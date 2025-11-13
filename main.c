#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <netdb.h>
#include <openssl/sha.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>

#ifdef _WIN32
#include <Windows.h>
#else
#include <unistd.h>
#endif

#define serverip "152.53.18.76"
const unsigned int serverport = 8080;
#define BUFFER_SIZE 256 // Generic buffer size for messages

// Note: itoa is non-standard. sprintf is a better, safer alternative.
char *itoa (int num, char *str) {
	/* Int to string */
	if (str == NULL) return NULL;
	sprintf(str, "%d", num);
	return str;
}

/**
 * Helper function to receive data until a newline is found or buffer is full.
 */
int receive_line(int socket_desc, char* buffer, size_t max_len) {
    memset(buffer, 0, max_len);
    int received_len = 0;
    char current_char;

    while (received_len < max_len - 1 && read(socket_desc, &current_char, 1) > 0) {
        buffer[received_len++] = current_char;
        if (current_char == '\n') {
            break; // Stop on newline
        }
    }
    buffer[received_len] = '\0'; // Ensure null termination

    if (received_len == 0 && read(socket_desc, &current_char, 1) <= 0) {
        return -1; // Connection closed or error
    }
    return received_len;
}


int main (int argc, char **argv) {
	time_t start_t, end_t;
	double diff_t;

	char job_message[64] = "JOB,";
	char* requested_difficulty = ",LOW";
	char serverversion[16]; // Increased size
	char serverreply[BUFFER_SIZE]; // Use generic buffer size
	char username[32] = "";

	unsigned int rejected_shares = 0;
	unsigned int accepted_shares = 0;
	unsigned int hashrate = 0;

	int socket_desc;
	struct sockaddr_in server;

	printf("\033[1;33md-cpuminer\n\033[1;35mby phantom32 and revox 2020-2021 modified arsnichydra 2025\n");
	printf("\033[0m----------\n");

	/* Create socket object */
	socket_desc = socket(AF_INET , SOCK_STREAM , 0);
	if (socket_desc == -1) {
		printf("Error: Couldn't create socket\n");
		return 1;
	}

	if (argc < 2) {
		printf("Enter your DUCO username (you can also pass it when launching the miner: ./d-cpuminer username): ");
		scanf("%s", username);
	}
	else {
		// Correct way to copy argv[1] into username buffer safely
		strncpy(username, argv[1], sizeof(username) - 1);
		username[sizeof(username) - 1] = '\0';
	}
	printf("Continuing as user %s\n", username);

	/* Establish connection to the server */
	printf("Connecting\n");
	server.sin_addr.s_addr = inet_addr(serverip);
	server.sin_family = AF_INET;
	server.sin_port = htons(serverport);

	if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
		printf("Error: Couldn't connect to the server\n");
		return 1;
	}
	else printf("Connected to the server\n");

    // Use receive_line to read the full server version message (e.g., "3.0\n")
	if (receive_line(socket_desc, serverversion, sizeof(serverversion)) <= 0) {
		printf("Error: Server version couldn't be received\n");
		return 1;
	}
    // Remove the newline for cleaner printing if needed
    serverversion[strcspn(serverversion, "\n")] = '\0'; 
	printf("Server is on version: %s\n\n", serverversion);

	/* Combine job request message */
	strcat(job_message, username);
	strcat(job_message, requested_difficulty);

	printf("Mining started using DUCO-S1 algorithm\n");

	while (1) {
		if (send(socket_desc, job_message, strlen(job_message), 0) < 0) {
			printf("Error: Couldn't send JOB message\n");
			return 1;
		}

        // Use receive_line to read the full job description
		if (receive_line(socket_desc, serverreply, sizeof(serverreply)) <= 0) {
			printf("Error: Couldn't receive job\n");
			return 1;
		}

        // printf("DEBUG: Received raw data: %s\n", serverreply); // Debug line

		/* Split received data - ADD NULL CHECKS TO PREVENT SEGFAULT */
		char* job = strtok (serverreply, ",");
		char* work = strtok (NULL, ",");
		char* diff = strtok (NULL, "");

		if (job == NULL || work == NULL || diff == NULL) {
            // Check for known server error messages
            if (strstr(serverreply, "o high") != NULL) {
                printf("Server reply: %s", serverreply);
            } else {
			    printf("Error: Failed to parse job data. Received bad string format.\n");
            }
			continue; // Skip mining and request a new job
		}
        
        // Ensure that 'diff' only contains digits before atoi, otherwise atoi behavior is undefined
        // (This check is advanced, we'll assume the server sends a valid number for now)


		char ducos1_result_string[16]; // Sufficient space for a result number

		time(&start_t); // measure starting time
		for (int i = 0; i < (100 * atoi(diff)) + 1; i++) {
			char str_to_hash[128]; // Sufficient space

            // Use snprintf for safe string concatenation/formatting
			snprintf(str_to_hash, sizeof(str_to_hash), "%s", job);
			itoa(i, ducos1_result_string);
			strncat(str_to_hash, ducos1_result_string, sizeof(str_to_hash) - strlen(str_to_hash) - 1);

			unsigned char temp[SHA_DIGEST_LENGTH];
			char buf[SHA_DIGEST_LENGTH * 2 + 1]; // +1 for null terminator
			memset(buf, 0x0, sizeof(buf));
			memset(temp, 0x0, sizeof(temp));
			SHA1((unsigned char *)str_to_hash, strlen(str_to_hash), temp);
			long iZ = 0;
			for (iZ = 0; iZ < SHA_DIGEST_LENGTH; iZ++)
				sprintf((char*) & (buf[iZ * 2]), "%02x", temp[iZ]);

			if (strcmp(work, buf) == 0) {
				/* Calculate hashrate */
				time(&end_t);
				diff_t = difftime(end_t, start_t);
                // Prevent division by zero if diff_t is 0
                if (diff_t == 0) diff_t = 1; 
				hashrate = (atoi(ducos1_result_string) / diff_t) / 1000;

				if (send(socket_desc, ducos1_result_string, strlen(ducos1_result_string), 0) < 0) { //send result
					printf("Error: Couldn't send result\n");
					return 1;
				}
                // Also send a newline character as required by most line-based protocols
                send(socket_desc, "\n", 1, 0); 


				char feedback[BUFFER_SIZE]; 
                // Use receive_line to get the feedback message
				if (receive_line(socket_desc, feedback, sizeof(feedback)) <= 0) { // receive feedback
					printf("Error: Feedback couldn't be received or connection closed\n");
					return 1;
				}

                // Remove newline for display
                feedback[strcspn(feedback, "\n")] = '\0';

				// Use strncmp for safer comparison
				if (strncmp("GOOD", feedback, 4) == 0 || strncmp("BLOCK", feedback, 5) == 0) {
					accepted_shares++;
					printf("Accepted share #%i (%s) %i kH/s\n", accepted_shares, ducos1_result_string, hashrate);

				} else if (strncmp("INVU", feedback, 4) == 0) {
					printf("Error: Incorrect username\n");
					return 1;

				} else {
					rejected_shares++;
					printf("Rejected share #%i (%s) %i kH/s\n", rejected_shares, ducos1_result_string, hashrate);
				}
				break; // Break the for loop after finding a share
			}
		}
	}
	return 0;
}

