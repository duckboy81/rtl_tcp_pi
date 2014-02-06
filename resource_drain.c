#include <stdio.h>
#include <time.h>


int main(int argc, char* argv[]) {

	time_t rawtime;
	struct tm* timeinfo;
	int secondwait = 5;

	if (argc > 1) {
		sscanf(argv[1], "%i", &secondwait);
	}

	printf("Waiting for %i seconds before each timestamp\n\n", secondwait);

	time(&rawtime);
	timeinfo = localtime(&rawtime);

	printf("Starting inf. loop! Timestamp: %s\n", asctime(timeinfo));

	while(1) {
		if (difftime(time(NULL), rawtime) >= secondwait) {
			time(&rawtime);
			timeinfo = localtime(&rawtime);
			printf("I'm still alive! Timestamp: %s", asctime(timeinfo));
		} //if(counter)
	} //while()

	printf("\n\n\n****************************\n\n");
	printf("Somehow I'm out of the inf. loop\n");

	return 0;
} //main()
