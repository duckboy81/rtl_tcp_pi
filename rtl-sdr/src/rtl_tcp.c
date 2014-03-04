/*** QUACK QUACK QUACK ***/

/*
 * rtl-sdr, turns your Realtek RTL2832 based DVB dongle into a SDR receiver
 * Copyright (C) 2012 by Steve Markgraf <steve@steve-m.de>
 * Copyright (C) 2012-2013 by Hoernchen <la@tfc-server.de>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

//Quack
#include <math.h>
#include <time.h>
#include <fftw3.h>
#include <bcm2835.h>

//Definitions to make it easier to program GPIO Pins
//	may add more if desired... see bcm2835.h
#define PIN03 RPI_GPIO_P1_03
#define PIN05 RPI_GPIO_P1_05
#define PIN07 RPI_GPIO_P1_07
#define PIN08 RPI_GPIO_P1_08
#define PIN10 RPI_GPIO_P1_10
#define PIN11 RPI_GPIO_P1_11

#define DETECTION_PIN PIN11
#define MAX_BINS_FOR_MEDIAN 100

#ifndef _WIN32
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <fcntl.h>
#else
#include "getopt/getopt.h"
#endif

#include <pthread.h>

#include "rtl-sdr.h"

//Quack - Debug global variable
static unsigned int ll_counter = 0;

static pthread_t ducky_fft_thread;

static pthread_cond_t exit_cond;
static pthread_mutex_t exit_cond_lock;

static pthread_mutex_t ll_mutex;
static pthread_cond_t cond;

struct llist {
    unsigned char *data;
	size_t len;
	struct llist *next;
};

typedef struct { /* structure size must be multiple of 2 bytes */
	char magic[4];
	uint32_t tuner_type;
	uint32_t tuner_gain_count;
} dongle_info_t;

static rtlsdr_dev_t *dev = NULL;

//Ducky: Added to allow "windowing" of resultant ffts
uint32_t desiredFreqLow = 0;
uint32_t desiredFreqHigh = 0;
long unsigned int desiredFFTPoints = (16 * 32 * 512); //This is the def length of each buffer from rtllib

//Ducky: Used for benchmarking
double max_value_difference_global = 0;

uint32_t thresholdFreqLow = 0;
uint32_t thresholdFreqHigh = 0;
double threshold_buffer = 0.5;

double calculatedHalfSpan = 0;

char enable_averaging = 1;

int global_numq = 0;
static struct llist *ll_buffers = 0;
int llbuf_num=500;

static volatile int do_exit = 0;

void usage(void)
{
	printf("rtl_tcp, an I/Q spectrum server for RTL2832 based DVB-T receivers. Can detect spikes in signals in certain freq window.\n"
		"Will ignore \"DC Spike\" by dropping first few FFT ouput values to 0.\n\n"
		"Usage:\n"
		"\t[-a set to any value to DISABLE sample averaging]\n"
		"\t[-f frequency to tune to [Hz]]\n"
		"\t[-g gain (default: 0 for auto)]\n"
		"\t[-s samplerate in Hz (default: 2048000 Hz)]\n"
		"\t[-b number of buffers (default: 32, set by library)]\n"
		"\t[-n max number of linked list buffers to keep (default: 500)]\n"
		"\t[-d device index (default: 0)]\n"
		"\t[-u Sets the buffer to add to the dynamic buffer when determining a detection (default: 0.5) [db?]]\n"
		"\t[-v Lower bound of theshold window [Hz] (must be specified if using -y/-z)]\n"
		"\t[-w Upper bound of theshold window [Hz] (must be specified if using -y/-z]\n"
        "\t[-x The number of data points to use for each FFT (default: 2^18)]\n"
        "\t FFTW recommends you set N to one of the following:\n"
        "\t  -N = 2^a\n"
        "\t  -N = 3^b\n"
        "\t  -N = 5^c\n"
        "\t  -N = 7^d\n"
        "\t  -N = 11^e || N = 13^f (where e+f is either 0 or 1) \n\t**Not sure what this means, this code will not compare N to this specific rule, so you may still get warnings following this recommendation.\n"
		"\t[-y Lower bound of FFT window [Hz]\n"
		"\t[-z Upper bound of FFT window [Hz]\n");
	exit(1);
} //usage()

#ifdef _WIN32
int gettimeofday(struct timeval *tv, void* ignored)
{
	FILETIME ft;
	unsigned __int64 tmp = 0;
	if (NULL != tv) {
		GetSystemTimeAsFileTime(&ft);
		tmp |= ft.dwHighDateTime;
		tmp <<= 32;
		tmp |= ft.dwLowDateTime;
		tmp /= 10;
#ifdef _MSC_VER
		tmp -= 11644473600000000Ui64;
#else
		tmp -= 11644473600000000ULL;
#endif
		tv->tv_sec = (long)(tmp / 1000000UL);
		tv->tv_usec = (long)(tmp % 1000000UL);
	}
	return 0;
}

BOOL WINAPI
sighandler(int signum)
{
	if (CTRL_C_EVENT == signum) {
		fprintf(stdout, "Signal caught, exiting!\n");
		do_exit = 1;
		rtlsdr_cancel_async(dev);
		return TRUE;
	}
	return FALSE;
}
#else
static void sighandler(int signum)
{
	fprintf(stdout, "Signal caught, exiting!\n");
	fprintf(stdout, "Max value difference global log10(output/threshold): %f\n", max_value_difference_global);
	rtlsdr_cancel_async(dev);
	bcm2835_gpio_clr(DETECTION_PIN);
	bcm2835_gpio_fsel(DETECTION_PIN, BCM2835_GPIO_FSEL_INPT);
	bcm2835_close();
	do_exit++;

    if (do_exit == 2) {
        fprintf(stdout, "Press CTRL+C again to hard close all operations\n");
    } else if (do_exit == 3) {
        fprintf(stdout, "\n\nHard exit! GO!\n");
        exit(0);
    } //if-else
}
#endif

void rtlsdr_callback(unsigned char *buf, uint32_t len, void *ctx)
{
int i;

	if(!do_exit) {
		struct llist *rpt = (struct llist*)malloc(sizeof(struct llist));
		rpt->data = (unsigned char*)malloc(len);

		memcpy(rpt->data, buf, len);

		rpt->len = len;
		rpt->next = NULL;

/* USED FOR TESTING DATA OUTPUT!
        FILE *test_fp = fopen("/home/pi/sample_data.txt", "w");

        for(i=0; i<len; i++) {
            fprintf(test_fp, "%u,", rpt->data[i]);
        }
        exit(0);
        do_exit = 1;
*/

		pthread_mutex_lock(&ll_mutex);

		if (ll_buffers == NULL) {
			ll_buffers = rpt;
		} else {
			struct llist *cur = ll_buffers;
			int num_queued = 0;

			while (cur->next != NULL) {
				cur = cur->next;
				num_queued++;
			}

			if(llbuf_num && llbuf_num == num_queued-2){
				struct llist *curelem;

				free(ll_buffers->data);
				curelem = ll_buffers->next;
				free(ll_buffers);
				ll_buffers = curelem;
			}

			cur->next = rpt;

 			if (num_queued > global_numq)
				printf("ll+, now %d\n", num_queued);
			else if (num_queued < global_numq)
				printf("ll-, now %d\n", num_queued);
			else
				printf("ll=, now %d\n", num_queued);

			global_numq = num_queued;
		}
		pthread_cond_signal(&cond);
		pthread_mutex_unlock(&ll_mutex);
	}
}

static void *ducky_fft(void *arg)
{
    struct llist *curelem, *prev;
    //int bytesleft, bytessent, index;
    struct timeval tv= {1,0};
    struct timespec ts;
    struct timeval tp;
    fd_set writefds;
    int r = 0;

    //Ducky: variables for fftw (pulled from fftw3_doc)
    fftw_complex *in, *out;
    fftw_plan p;
    double curr_output[desiredFFTPoints], max_value_threshold, threshold_array[MAX_BINS_FOR_MEDIAN + 1];
	long double sum_threshold_maxes;
	double old_fft[desiredFFTPoints][2];
    long unsigned int i,j, curr_data_point = 0, curr_max_threshold = 0, threshold_array_transition = 0;

    //Ducky: Filter results to narrow band
    uint32_t tunedFreqCenter = rtlsdr_get_center_freq(dev);
    uint32_t span = rtlsdr_get_sample_rate(dev);
    uint32_t lowerBound = tunedFreqCenter - span/2;

	//TODO: Remove this test code
	double max_value_difference = 0;
	double max_value_difference_old = 0;
	struct timeval sample0, sample1, sample2, sample3, sample4, sample5, sample6, sample7, sample8;

	//Convert dB to a decimal value
	threshold_buffer = pow(10, threshold_buffer);

    //Calculate upper/lower array bounds
    //Calculations derived on page 41 of Ducky's notebook
    //Attempts to add 3% buffer edge to each side
    long double pos_temp = 0;

    unsigned long int lower_pos = 0;
    unsigned long int upper_pos = desiredFFTPoints - 1;

	unsigned long int threshold_lower_pos = 0;
	unsigned long int threshold_upper_pos = desiredFFTPoints - 1;

    if (desiredFreqLow != 0) {
        pos_temp = desiredFreqLow - lowerBound;
        pos_temp = pos_temp/span;
        pos_temp = pos_temp * desiredFFTPoints;
        pos_temp = pos_temp - 0.01*desiredFFTPoints;

        lower_pos = (unsigned long int) pos_temp;
    } //if()

    if (desiredFreqHigh != 0) {
        pos_temp = desiredFreqHigh - lowerBound;
        pos_temp = pos_temp/span;
        pos_temp = pos_temp * desiredFFTPoints;
        pos_temp = pos_temp + 0.01*desiredFFTPoints;

        upper_pos = (unsigned long int) pos_temp;
    } //if()

	if (thresholdFreqLow != 0) {
        pos_temp = thresholdFreqLow - lowerBound;
        pos_temp = pos_temp/span;
        pos_temp = pos_temp * desiredFFTPoints;
        pos_temp = pos_temp - 0.01*desiredFFTPoints;

        threshold_lower_pos = (unsigned long int) pos_temp;
	} //if()

	if (thresholdFreqHigh != 0) {
        pos_temp = thresholdFreqHigh - lowerBound;
        pos_temp = pos_temp/span;
        pos_temp = pos_temp * desiredFFTPoints;
        pos_temp = pos_temp + 0.01*desiredFFTPoints;

        threshold_upper_pos = (unsigned long int) pos_temp;
	} //if()

    //Check for out of array
    if (lower_pos > desiredFFTPoints - 1) {
        lower_pos = desiredFFTPoints - 1;
    } //if()

    //Check for out of array
    if (upper_pos > desiredFFTPoints - 1) {
        upper_pos = desiredFFTPoints - 1;
    } //if()

    //Check for out of array
    if (threshold_lower_pos > desiredFFTPoints - 1) {
        threshold_lower_pos = desiredFFTPoints - 1;
    } //if()

    //Check for out of array
    if (threshold_upper_pos > desiredFFTPoints - 1) {
        threshold_upper_pos = desiredFFTPoints - 1;
    } //if()


printf("\nf0: %u\n", tunedFreqCenter);
printf("span: %u\n", span);
printf("lower_pos: %lu\n", lower_pos);
printf("upper_pos: %lu\n", upper_pos);
printf("threshold_lower_pos: %lu\n", threshold_lower_pos);
printf("threshold_upper_pos: %lu\n", threshold_upper_pos);
printf("desiredFreqP: %lu\n", desiredFFTPoints);
printf("desiredFreqH: %u\n", desiredFreqHigh);
printf("desiredFreqL: %u\n\n", desiredFreqLow);

	//TODO: REMOVE THIS!
//    FILE *sample_file = fopen("/home/pi/sample_data_new.txt", "w");
    FILE *test_file = fopen("/home/pi/fft_output.txt", "w");

	printf("\nAbout to enter ducky land!\n");

    //Setup fftw
    in = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * desiredFFTPoints);
    out = (fftw_complex*) fftw_malloc(sizeof(fftw_complex) * desiredFFTPoints);
    p = fftw_plan_dft_1d(desiredFFTPoints, in, out, FFTW_FORWARD, FFTW_MEASURE);


    int mutex_lock_check = 0;
    struct timespec abs_time;


	printf("Initializing DETECTION_PIN to LOW!\n");
	//bcm2835_gpio_write(DETECTION_PIN, LOW);
	bcm2835_gpio_fsel(DETECTION_PIN, BCM2835_GPIO_FSEL_OUTP);
	bcm2835_gpio_write(DETECTION_PIN, LOW);


double quickMax = 0;
double quickMin = 999;


	while(!do_exit) {
		//if (do_exit) {
        //    sighandler(0);
		//	pthread_exit(NULL);
		//} //if()
		gettimeofday(&sample0, NULL);

		do {
            clock_gettime(CLOCK_REALTIME , &abs_time);
		    abs_time.tv_sec += 5;

		    mutex_lock_check = pthread_mutex_timedlock(&ll_mutex, &abs_time);
//		    mutex_lock_check = pthread_mutex_lock(&ll_mutex);

		    if (do_exit) {
			break;
		    } //if()
		} while(mutex_lock_check != 0);
		//gettimeofday(&tp, NULL);
		//ts.tv_sec = tp.tv_sec + 5;
		//ts.tv_nsec = tp.tv_usec * 1000;
		//r = pthread_cond_timedwait(&cond, &ll_mutex, &ts);
		//if (r == ETIMEDOUT) {
		//	pthread_mutex_unlock(&ll_mutex);
		//	printf("fft worker condition timeout\n");
		//	sighandler(0);
		//	pthread_exit(NULL);
        //} //if()

		curelem = ll_buffers;
		ll_buffers = 0;
		pthread_mutex_unlock(&ll_mutex);

		//Initial condition check
		if (curelem != 0) {
			printf("Putting samples into FFT array\n");
			gettimeofday(&sample1, NULL);
		} //if()

        while(curelem != 0) {
		    if (do_exit) {
				break;
		    } //if()

			/* Should be no need for these 3 lines */
            //bytesleft = curelem->len;
            //index = 0;
            //bytessent = 0;

            //Convert real data to reals and imaginaries and store in array
            for(j=1; j < curelem->len; j=j+2) {

				//Subtract 128 to ensure data is centered on 0
				//if (enable_averaging) {
	            //    in[curr_data_point][0] = (curelem->data[j-1] - 128) + old_samples[curr_data_point][0];
	            //    in[curr_data_point][1] = (curelem->data[j] - 128) + old_samples[curr_data_point][1];
				//} else {
    	        in[curr_data_point][0] = (curelem->data[j-1] - 128);
        	    in[curr_data_point][1] = (curelem->data[j] - 128);
				//}


				//Copy current data into history
				//old_samples[curr_data_point][0] = curelem->data[j-1] - 128;
				//old_samples[curr_data_point][1] = curelem->data[j] - 128;


                curr_data_point++;

                //For testing -> Print samples to a file
                //fprintf(sample_file, "%u, %u,", curelem->data[j-1], curelem->data[j]);

                //Is it time to crunch FFTs yet?
				if (curr_data_point >= desiredFFTPoints) {

					gettimeofday(&sample2, NULL);

                    //Reset variable to reuse it
                    curr_data_point = 0;

                    //This is a test to see if this function if a blocking function
                    fprintf(stdout, "Cruncing FFT...!  ");
					gettimeofday(&sample3, NULL);
                    fftw_execute(p); //Repeat as needed
					gettimeofday(&sample4, NULL);
                    fprintf(stdout, "...finished crunching!\n");

                    //Need to FFTShift manually!
                    //Calculate magnitude of results (combine im with real)
					printf("Calculating Magnitudes while FFT Shifting\n");
					gettimeofday(&sample5, NULL);
                    for(i=desiredFFTPoints/2; i < desiredFFTPoints; i++) {

						if (enable_averaging) {
	                        curr_output[curr_data_point] = pow(out[i][0] + old_fft[i][0], 2) + pow(out[i][1] + old_fft[i][1], 2);
						} else {
	                        curr_output[curr_data_point] = pow(out[i][0], 2) + pow(out[i][1], 2);
						} //if-else()

						//Write output to history
						old_fft[i][0] = out[i][0];
						old_fft[i][1] = out[i][1];

                        curr_data_point++;
                    } //for()

                    for(i=0; i < desiredFFTPoints/2; i++) {

						if (enable_averaging) {
	                        curr_output[curr_data_point] = pow(out[i][0] + old_fft[i][0], 2) + pow(out[i][1] + old_fft[i][1], 2);
						} else {
	                        curr_output[curr_data_point] = pow(out[i][0], 2) + pow(out[i][1], 2);
						} //if-else()

						//Write output to history
						old_fft[i][0] = out[i][0];
						old_fft[i][1] = out[i][1];

                        curr_data_point++;
                    } //for()

					//Ignore first 5 output data points, now centered (get rid of the "DC Spike" or so I know it as)
					//for(i=desiredFFTPoints/4; i<desiredFFTPoints/4 + 5; i++) { curr_output[i] = 0; }

					gettimeofday(&sample6, NULL);

					printf("Finding average threshold max\n");

					//Get max signal in threshold
					//max_value_threshold = 0;
					sum_threshold_maxes = 0;
					max_value_difference = 0.0;
					curr_max_threshold = 0;
					threshold_array_transition = (threshold_upper_pos - threshold_lower_pos) / MAX_BINS_FOR_MEDIAN;

					if (thresholdFreqLow != 0 || thresholdFreqHigh != 0) {
						for(i=threshold_lower_pos; i<threshold_upper_pos; i++) {
							if (i > (threshold_array_transition * curr_max_threshold) + threshold_lower_pos) {
								curr_max_threshold++;
							} //if()

							if (curr_output[i] > threshold_array[curr_max_threshold]) {
								threshold_array[curr_max_threshold] = curr_output[i];
							} //if()
						} //for()
					} //if()

					for (i=0; i<curr_max_threshold; i++) {
						sum_threshold_maxes += threshold_array[i];
					} //for()

					max_value_threshold = sum_threshold_maxes / curr_max_threshold;

					gettimeofday(&sample7, NULL);
					printf("Checking window for spike.\n");
                    //Check window for signal
                    if (max_value_threshold > 0) {
                        for(i=lower_pos; i<upper_pos; i++) {
							//TODO: Used for testing -- seeing what the max value difference was...
							if ( curr_output[i] / max_value_threshold > max_value_difference) {
								max_value_difference = curr_output[i] / max_value_threshold;
							} //if()

							//Note: threshold_buffer is converted to decimal value earlier in this function.
                            if ( curr_output[i] / max_value_threshold > threshold_buffer) {
                                //do_exit = 1;

                                fprintf(stdout, "*** I see a signal! ***\n");

								//Ensures uController sees the pulse (~0.1 ms delay)
								if (bcm2835_gpio_lev(DETECTION_PIN) == HIGH) {
									bcm2835_gpio_write(DETECTION_PIN, LOW);
									bcm2835_delayMicroseconds(90);
								} //if()

								printf("Setting DETECTION_PIN HIGH\n");
								bcm2835_gpio_write(DETECTION_PIN, HIGH);
								//bcm2835_delay(1000);

								//Set to 1 for print to file on detection, 0 for no print to file
								if (0) {
									fprintf(stdout, "\nPrinting data to file...\n");

									//Add large spike to signify search bounderies
									curr_output[lower_pos] = 10000000000000;
									curr_output[upper_pos] = 10000000000000;
									curr_output[threshold_lower_pos] = 10000000000000;
									curr_output[threshold_upper_pos] = 10000000000000;

                    	            //For testing -> Print ffts to a file
                        	        for(i=0; i < desiredFFTPoints; i++) {
                            	        fprintf(test_file, "%f,", curr_output[i]);
	                                } //for()
    	                            fprintf(stdout, "...done\n\nGoodbye!\n\n");

        	                        do_exit = 1;
								} //if()

								//exit(0);

								break;
/*                            } else if ( curr_output[i] / max_value_threshold < pow(10,-1.0)) { //WHY DOES THIS HAPPEN!?!?!

								//Set to 1 for print to file on detection, 0 for no print to file
								if (1) {
									fprintf(stdout, "\nPrinting data to file...\n");

									//Add large spike to signify search bounderies
									curr_output[lower_pos] = 10000000000000;
									curr_output[upper_pos] = 10000000000000;
									curr_output[threshold_lower_pos] = 10000000000000;
									curr_output[threshold_upper_pos] = 10000000000000;

                    	            //For testing -> Print ffts to a file
                        	        for(i=0; i < desiredFFTPoints; i++) {
                            	        fprintf(test_file, "%f,", curr_output[i]);
	                                } //for()
    	                            fprintf(stdout, "...done\n\nGoodbye!\n\n");

        	                        do_exit = 1;
								} //if()

*/
                            } else {
								if (bcm2835_gpio_lev(DETECTION_PIN) != LOW) {
									printf("Setting DETECTION_PIN LOW\n");
									bcm2835_gpio_write(DETECTION_PIN, LOW);
								} //if()
							} //if-else()
                        } //for()

						//Set to 1 for continual update of max ratio
						if (1) {
							if (log10(max_value_difference) > max_value_difference_global) {
								max_value_difference_global = log10(max_value_difference);
							} //if()
						} //if()

						//Clear the console
						system("clear");

						//printf("Max SNR (output/threshold): %f\n", max_value_difference);
						printf("Max SNR log10(output/threshold): %f\n", log10(max_value_difference));
						printf("Max SNR log10(output/threshold): %f [i-1]\n", log10(max_value_difference_old));
						printf("Max value difference global log10(output/threshold): %f\n", max_value_difference_global);

                    } else {
						printf("No threshold! Threshold reported as  <= 0\n");
					} //if()

                    //Reset variables
                    curr_data_point = 0;
                    //freopen(NULL, "w", sample_file);
                    //freopen(NULL, "w", test_file);

					//TODO: Remove, used for timing analysis
					//Copy current data into history
					max_value_difference_old = max_value_difference;

					gettimeofday(&sample8, NULL);


		printf("\n***** Time log! *****\n");
		printf("-Mutex locking global linked list: %f (ms)\n", (double) (sample1.tv_usec - sample0.tv_usec) / 1000000 + (double) (sample1.tv_sec - sample0.tv_sec)); //1-0
		printf("-Inputing samples into FFT array & history: %f (ms)\n", (double) (sample2.tv_usec - sample1.tv_usec) / 1000000 + (double) (sample2.tv_sec - sample1.tv_sec)); //2-1
		printf("-Crunching FFT: %f (ms)\n", (double) (sample4.tv_usec - sample3.tv_usec) / 1000000 + (double) (sample4.tv_sec - sample3.tv_sec)); //4-3
		printf("-Calculating magnitude and FFT Shift: %f (ms)\n", (double) (sample6.tv_usec - sample5.tv_usec) / 1000000 + (double) (sample6.tv_sec - sample5.tv_sec)); //6-5
		printf("-Threshold find: %f (ms)\n", (double) (sample7.tv_usec - sample6.tv_usec) / 1000000 + (double) (sample7.tv_sec - sample6.tv_sec)); //7-6
		printf("-Above threshold comparisons: %f (ms)\n", (double) (sample8.tv_usec - sample7.tv_usec) / 1000000 + (double) (sample8.tv_sec - sample7.tv_sec)); //8-7
		printf("-Total: %f (ms)\n\n", (double) (sample8.tv_usec - sample1.tv_usec) / 1000000 + (double) (sample8.tv_sec - sample1.tv_sec)); //8-1


                } //if()
            } //for(each data point in buffer)

            prev = curelem;
            curelem = curelem->next;
            free(prev->data);
            free(prev);
        } //while(we have not reached the end of the buffer)

	} //while()

//    fclose(sample_file);
//    fclose(test_file);

    fftw_destroy_plan(p);
    fftw_free(in);
    fftw_free(out);

    return 0;

} //ducky_fft


static int set_gain_by_index(rtlsdr_dev_t *_dev, unsigned int index)
{
	int res = 0;
	int* gains;
	int count = rtlsdr_get_tuner_gains(_dev, NULL);

	if (count > 0 && (unsigned int)count > index) {
		gains = malloc(sizeof(int) * count);
		count = rtlsdr_get_tuner_gains(_dev, gains);

		res = rtlsdr_set_tuner_gain(_dev, gains[index]);

		free(gains);
	}

	return res;
}

int main(int argc, char **argv)
{
	int r, opt, i;
	uint32_t frequency = 100000000, samp_rate = 2048000;
	struct sockaddr_in local, remote;
	int device_count;
	uint32_t dev_index = 0, buf_num = 0;
	int gain = 0;
	struct llist *curelem,*prev;
	pthread_attr_t attr;
	void *status;
	struct timeval tv = {1,0};
	struct linger ling = {1,0};
	fd_set readfds;
	u_long blockmode = 1;
	dongle_info_t dongle_info;
#ifdef _WIN32
	WSADATA wsd;
	i = WSAStartup(MAKEWORD(2,2), &wsd);
#else
	struct sigaction sigact, sigign;
#endif

	while ((opt = getopt(argc, argv, "a:d:f:g:s:b:n:v:w:u:y:x:z:")) != -1) {
		switch (opt) {
		case 'a':
			enable_averaging = 0;
			printf("Disabling averaging\n");
			break;
		case 'd':
			dev_index = atoi(optarg);
			break;
		case 'f':
			frequency = (uint32_t)atof(optarg);
			break;
		case 'g':
			gain = (int)(atof(optarg) * 10); /* tenths of a dB */
			break;
		case 's':
			samp_rate = (uint32_t)atof(optarg);
			break;
		case 'b':
			buf_num = atoi(optarg);
			break;
		case 'n':
			llbuf_num = atoi(optarg);
			printf("Max buffers set to: %d\n", llbuf_num);
			break;
		case 'v':
			thresholdFreqLow = (uint32_t) atoi(optarg);
			break;
		case 'w':
			thresholdFreqHigh = (uint32_t) atoi(optarg);
			break;
		case 'u':
			threshold_buffer = (double) atof(optarg);
			break;
        case 'y':
            desiredFreqLow = (uint32_t) atoi(optarg);
            printf("Setting lower bound of fft window %iHz\n", desiredFreqLow);
            break;
        case 'x':
            desiredFFTPoints = (long unsigned int) atoi(optarg);
            printf("Number of points for each FFT: %lu", desiredFFTPoints);
            break;
        case 'z':
            desiredFreqHigh = (uint32_t) atoi(optarg);
            printf("Setting upper bound of fft window %iHz\n", desiredFreqHigh);
            break;
		default:
			usage();
			break;
		}
	}

	if (argc < optind)
		usage();

	device_count = rtlsdr_get_device_count();
	if (!device_count) {
		fprintf(stdout, "No supported devices found.\n");
		exit(1);
	}

	printf("Found %d device(s).\n", device_count);
	printf("Threshold buffer is 10^%f\n", threshold_buffer);

	rtlsdr_open(&dev, dev_index);
	if (NULL == dev) {
	fprintf(stdout, "Failed to open rtlsdr device #%d.\n", dev_index);
		exit(1);
	}

	printf("Using %s\n", rtlsdr_get_device_name(dev_index));
#ifndef _WIN32
	sigact.sa_handler = sighandler;
	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigign.sa_handler = SIG_IGN;
	sigaction(SIGINT, &sigact, NULL);
	sigaction(SIGTERM, &sigact, NULL);
	sigaction(SIGQUIT, &sigact, NULL);
	sigaction(SIGPIPE, &sigign, NULL);
#else
	SetConsoleCtrlHandler( (PHANDLER_ROUTINE) sighandler, TRUE );
#endif
	/* Set the sample rate */
	r = rtlsdr_set_sample_rate(dev, samp_rate);
	if (r < 0)
		fprintf(stdout, "WARNING: Failed to set sample rate.\n");

    /* Ducky: Warn of possibly un-optimal FFT sample usage */
    if ( (desiredFFTPoints % 2) && (desiredFFTPoints % 3) &&
         (desiredFFTPoints % 5) && (desiredFFTPoints % 7) ) {
        fprintf(stdout, "WARNING: The number of samples per FFT may not be optimal.  FFTW3 recommends"
                        " the number of samples fall under one of these rules.\n"
                        " -N = 2^a\n"
                        " -N = 3^b\n"
                        " -N = 5^c\n"
                        " -N = 7^d\n"
                        " -N = 11^e || N = 13^f (where e+f is either 0 or 1) **Not sure what this means, this code will not compare N to this specific rule, so you may still get warnings following this recommendation.\n");
    } //if()


    /* Ducky: Calculate requested span, compare to sample rate */
    calculatedHalfSpan = rtlsdr_get_sample_rate(dev) / 2;

    if (desiredFreqHigh && (desiredFreqHigh > (frequency + calculatedHalfSpan) || desiredFreqHigh < (frequency - calculatedHalfSpan))) {
        fprintf(stdout, "Desired upper bound is outside the output span.\n \
                         For this setup, the upper bound should fall within: (%f, %f)Hz\n \
                         Disregarding upper bound request...\n",
                         frequency-calculatedHalfSpan,
                         frequency+calculatedHalfSpan);
        desiredFreqHigh = 0;
    } //if()

    if (desiredFreqLow && (desiredFreqLow > (frequency + calculatedHalfSpan) || desiredFreqLow < (frequency - calculatedHalfSpan))) {
        fprintf(stdout, "Desired lower bound is outside the output span.\n \
                         For this setup, the lower bound should fall within: (%f, %f)Hz\n \
                         Disregarding lower bound request...\n",
                         frequency-calculatedHalfSpan,
                         frequency+calculatedHalfSpan);
        desiredFreqLow = 0;
    } //if()

    if (thresholdFreqLow && (thresholdFreqLow > (frequency + calculatedHalfSpan) || thresholdFreqLow < (frequency - calculatedHalfSpan))) {
        fprintf(stdout, "Desired threshold lower bound is outside the output span.\n \
                         For this setup, the threshold lower bound should fall within: (%f, %f)Hz\n \
                         Disregarding threshold lower bound request...\n",
                         frequency-calculatedHalfSpan,
                         frequency+calculatedHalfSpan);
        thresholdFreqLow = 0;
    } //if()

    if (thresholdFreqHigh && (thresholdFreqHigh > (frequency + calculatedHalfSpan) || thresholdFreqHigh < (frequency - calculatedHalfSpan))) {
        fprintf(stdout, "Desired threshold upper bound is outside the output span.\n \
                         For this setup, the threshold upper bound should fall within: (%f, %f)Hz\n \
                         Disregarding threshold upper bound request...\n",
                         frequency-calculatedHalfSpan,
                         frequency+calculatedHalfSpan);
        thresholdFreqHigh = 0;
    } //if()

    if (!desiredFreqLow && !desiredFreqHigh) {
        fprintf(stdout, "WARNING: No freq window specified, will not search for signal!! Specify with -y & -z params\n");
    } //if()


	//Check for proper threshold window
	if ((desiredFreqLow || desiredFreqHigh) &&
		!(thresholdFreqHigh < desiredFreqLow) &&
		!(desiredFreqHigh < thresholdFreqLow)) {
		fprintf(stdout, "WARNING: Your FFT window overlaps with your threshold window\n");
	} //if()

	//Check to see if threshold window was even set
	if ((desiredFreqLow || desiredFreqHigh) && !thresholdFreqLow && !thresholdFreqHigh) {
		fprintf(stdout, "WARNING: You specified an FFT window, but failed to specify a threshold window.  No detection algorithm will run\n");
	} //if()



	/* Set the frequency */
	r = rtlsdr_set_center_freq(dev, frequency);
	if (r < 0)
		fprintf(stdout, "WARNING: Failed to set center freq.\n");
	else
		fprintf(stdout, "Tuned to %i Hz.\n", frequency);

	if (0 == gain) {
		 /* Enable automatic gain */
		r = rtlsdr_set_tuner_gain_mode(dev, 0);
        fprintf(stdout, "Enabling automatic gain...\n");
		if (r < 0)
			fprintf(stdout, "WARNING: Failed to enable automatic gain.\n");
	} else {
		/* Enable manual gain */
		r = rtlsdr_set_tuner_gain_mode(dev, 1);
		if (r < 0)
			fprintf(stdout, "WARNING: Failed to enable manual gain.\n");

		/* Set the tuner gain */
		r = rtlsdr_set_tuner_gain(dev, gain);
		if (r < 0)
			fprintf(stdout, "WARNING: Failed to set tuner gain.\n");
		else
			fprintf(stdout, "Tuner gain set to %f dB.\n", gain/10.0);
	}

	/* Reset endpoint before we start reading from it (mandatory) */
	r = rtlsdr_reset_buffer(dev);
	if (r < 0)
		fprintf(stdout, "WARNING: Failed to reset buffers.\n");

	pthread_mutex_init(&exit_cond_lock, NULL);
	pthread_mutex_init(&ll_mutex, NULL);
	pthread_mutex_init(&exit_cond_lock, NULL);
	pthread_cond_init(&cond, NULL);
	pthread_cond_init(&exit_cond, NULL);

	//DUCKY: TOOK OUT WHILE LOOP BECAUSE THE PROGRAM WOULD
	//	STOP RESPONDING IF IT LOST THE SOCKET
	//while(1) {

        printf("\nSkipping any TCP connection requirement\n");

		printf("\nInitializing GPIO pin library (bcm2835)");
		if (!bcm2835_init()) {
			printf("\nCould not initialize bcm2835!!");
			return 1;
		} //if()

		memset(&dongle_info, 0, sizeof(dongle_info));
		memcpy(&dongle_info.magic, "RTL0", 4);

		r = rtlsdr_get_tuner_type(dev);
		if (r >= 0)
			dongle_info.tuner_type = htonl(r);

		r = rtlsdr_get_tuner_gains(dev, NULL);
		if (r >= 0)
			dongle_info.tuner_gain_count = htonl(r);

		pthread_attr_init(&attr);
		pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);

		//Ducky: Added our own FFT
		r = pthread_create(&ducky_fft_thread, &attr, ducky_fft, NULL);

		pthread_attr_destroy(&attr);

		r = rtlsdr_read_async(dev, rtlsdr_callback, NULL, buf_num, 0);

		//Ducky: Added our own FFT
		pthread_join(ducky_fft_thread, &status);

		printf("all threads dead..\n");
		curelem = ll_buffers;
		ll_buffers = 0;

		while(curelem != 0) {
			prev = curelem;
			curelem = curelem->next;
			free(prev->data);
			free(prev);
		}

		do_exit = 0;
		global_numq = 0;
	//}

out:
	rtlsdr_close(dev);
#ifdef _WIN32
	WSACleanup();
#endif
	printf("bye!\n");
	return r >= 0 ? r : -r;
}
