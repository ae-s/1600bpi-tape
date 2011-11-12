/* phase modulation decoder for 1600bpi magnetic tape
 * Copyright 2011, Duncan Smith
 * I haven't chosen a license yet.  For now, it's mine.
 */

/* -s --speed <inches-per-second>
 * -o --output <dirname>
 * -v --verbose
 */

#define __USE_GNU

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <audiofile.h>

double bpi = 1600; // bits per inch
double speed = 15; // inches per second
int rate = 44100 * 4; // samples per second

double samples_per_baud; // = rate / (bpi * speed);

// samples per cycle of the carrier wave
double c_freq; // = 1/samples_per_baud

/* values returned by the bit discriminator.  meant to be or'd
 * together.
 */

#define BIT_KNOWN	2	/* data detected, definitely */
#define BIT_UNKNOWN	0	/* data detected, maybe */
#define BIT_NOTHING	4	/* no data detected */
#define BIT_ONE		1	/* found a one */
#define BIT_ZERO	0	/* found a zero */

#define WIN_SIZE	400	/* sliding window to remove DC offset with */
float window[WIN_SIZE];


int verbose = 0;
char *output = NULL;

int options(int argc, char *argv[])
{
	static struct option long_options[] = {
		{"speed", 1, NULL, 's'},
		{"output", 1, NULL, 'o'},
		{"verbose", 0, NULL, 'v'},
		{NULL, 0, NULL, 0}};

	while (1) {
		int c;
		c = getopt_long(argc, argv, "s:o:v", long_options, &optind);

		switch (c) {
		case -1:
			return optind;
		case 's':
			speed = strtod(optarg, NULL);
			break;
		case 'o':
			output = optarg;
			break;
		case 'v':
			verbose = 1;
			break;
		case '?':
			puts("INVALID ARGUMENT");
			exit(EXIT_FAILURE);
		default:
			puts("INTERNAL ERROR");
			exit(EXIT_FAILURE);
		}
	}

	return 0;
}

int main(int argc, char *argv[])
{
	AFfilehandle input;

	argv += options(argc, argv);
	input = afOpenFile(argv[0], "r", NULL);
	if (input == AF_NULL_FILEHANDLE) {
		printf("Unable to open %s\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	if (chdir(output) == -1) {
		perror(NULL);
		printf("(Make sure %s is a directory)\n", output);
		exit(EXIT_FAILURE);
	}

	afSetVirtualSampleFormat(input, AF_DEFAULT_TRACK, AF_SAMPFMT_FLOAT, 32);
	return processfile(input);
}

int processfile(AFfilehandle input)
{
	float *data;
	int s2;
	int size = afGetFrameCount(input, AF_DEFAULT_TRACK);

	rate = afGetRate(input, AF_DEFAULT_TRACK);
	samples_per_baud = rate / (bpi * speed);
	c_freq = 1/samples_per_baud;

	printf("frequency = %lf cyc/samp, %lf samp/syc, %lf hz\n", c_freq, samples_per_baud, 44100/samples_per_baud);

	data = malloc(sizeof(float) * size);
	if ((s2 = afReadFrames(input, AF_DEFAULT_TRACK, data, size)) != size) {
		free(data);
		printf("Read %d frames (expecting %d)\n", s2, size);
		return -1;
	}
	demod(data, size);
	write_output(data, size);
	return 0;
}

int get_next(int which, float data[], int data_len)
{
	return 0;
}

int write_output(float data[], int data_len)
{
	AFfilesetup fs;
	AFfilehandle fh;
	fs = afNewFileSetup();
	afInitFileFormat(fs, AF_FILE_WAVE);
	afInitRate(fs, AF_DEFAULT_TRACK, rate * 2);
	afInitChannels(fs, AF_DEFAULT_TRACK, 1);
	fh = afOpenFile("out.wav", "w", fs);
	afSetVirtualSampleFormat(fh, AF_DEFAULT_TRACK, AF_SAMPFMT_FLOAT, 0);
	afSetVirtualChannels(fh, AF_DEFAULT_TRACK, 1);
	afWriteFrames(fh, AF_DEFAULT_TRACK, data, data_len);
	afCloseFile(fh);
	return 0;
}

/* stub demodulator.  Compares to C.
 */
int demod(float data[], int data_len)
{
	int i;

	float max = 0;

	// Find the max value, scale other values to fit
	for (i = 0; i < data_len; i++)
		if (fabsf(data[i]) > max)
			max = fabsf(data[i]);

	for (i = 0; i < data_len; i++) {
		data[i] *= sin((double) c_freq * i / (2 * M_PI)) / max;
	}

	printf("Altered %d samples, max = %f\n", i, max);
}

int integrate(float data[], int data_len)
{
	int i,j;
	double sum = 0;
	double rms_sum = 0;
	double sum_max = 0;
	double max = 0;
	for (i = 0; i < data_len; i++) {
		sum += data[i];
		rms_sum += data[i] * data[i];
		data[i] = (float)sum;
		if (i % 1000000 == 0)
			printf("Running sum at %d = %lf\nRunning RMS at %d = %lf\n", i, sum, i, sqrt(rms_sum/i));
		if (sum > sum_max)
			sum_max = sum;
	}

	printf("Dividing by %lf\n", sum_max);

	for (i = 0; i < data_len; i++) {
		float window_sum = 0;
		data[i] /= sum_max;
		if (data[i] > max)
			max = data[i];

		if (i > WIN_SIZE)
			for (j = (i - WIN_SIZE); j < i; j++)
				window_sum += data[j];

		printf("Subtracting %f from %d %d\n", window_sum/WIN_SIZE, i, j);
		data[i] -= window_sum/WIN_SIZE;
	}
	printf("Max is now %lf\n", max);
}
