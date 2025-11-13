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

#define serverhost "152.53.18.76"
const unsigned int serverport = 8080;

char *itoa(int num, char *str) {
    if (str == NULL) return NULL;
    sprintf(str, "%d", num);
    return str;
}

int main(int argc, char **argv) {
    time_t start_t, end_t;
    double diff_t;

    char job_message[128] = "";
    char requested_difficulty[] = "LOW";
    char serverversion[8] = {0};
    char serverreply[40 + 1 + 40 + 1 + 48 + 1];
    char username[64] = "";
    char mining_key[64] = "";
    char buffer[256];

    unsigned int rejected_shares = 0;
    unsigned int accepted_shares = 0;
    unsigned int hashrate = 0;

    int socket_desc;
    struct sockaddr_in server;
    struct hostent *he;

    printf("\033[1;33md-cpuminer\n\033[1;35mby phantom32 and revox 2020-2021 (updated by arsnichydra 2025)\n");
    printf("\033[0m----------\n");

    socket_desc = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_desc == -1) {
        printf("Error: Couldn't create socket\n");
        return 1;
    }

    if (argc < 2) {
        printf("Enter your DUCO username (you can also pass it when launching the miner ./d-cpuminer username [mining_key]): ");
        scanf("%63s", username);
    } else {
        snprintf(username, sizeof(username), "%s", argv[1]);
    }
    printf("Continuing as user %s\n", username);

    if (argc >= 3) {
        snprintf(mining_key, sizeof(mining_key), "%s", argv[2]);
    } else {
        char *kp = getpass("Enter your mining key (will not echo): ");
        if (kp == NULL || strlen(kp) == 0) {
            printf("Error: No mining key provided\n");
            return 1;
        }
        snprintf(mining_key, sizeof(mining_key), "%s", kp);
    }
    printf("Using a mining key of length %zu characters\n", strlen(mining_key));

    printf("Resolving host: %s\n", serverhost);
    he = gethostbyname(serverhost);
    if (he == NULL) {
        herror("Error resolving hostname");
        return 1;
    }

    printf("Connecting\n");
    memset(&server, 0, sizeof(server));
    server.sin_family = AF_INET;
    server.sin_port = htons(serverport);
    memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);

    if (connect(socket_desc, (struct sockaddr *)&server, sizeof(server)) < 0) {
        perror("Error: Couldn't connect to the server");
        return 1;
    } else {
        printf("Connected to the server\n");
    }

    // Send LOGIN packet
    snprintf(buffer, sizeof(buffer), "LOGIN,%s,%s,%s,%s,%s\n",
             username, mining_key, "LinuxMiner", "1.0", "CustomC");
    if (send(socket_desc, buffer, strlen(buffer), 0) < 0) {
        perror("Error sending LOGIN command");
        close(socket_desc);
        return 1;
    }
    printf("Login packet sent for %s\n", username);

    // Receive server version
    if (recv(socket_desc, serverversion, sizeof(serverversion) - 1, 0) < 0) {
        printf("Error: Server version couldn't be received\n");
        return 1;
    } else {
        printf("Server is on version: %s\n\n", serverversion);
    }

    // Prepare job message
    snprintf(job_message, sizeof(job_message), "JOB,%s,%s,%s,%s",
             username, requested_difficulty, mining_key, "C-Miner");

    printf("Mining started using DUCO-S1 algorithm\n");

    while (1) {
        if (send(socket_desc, job_message, strlen(job_message), 0) < 0) {
            printf("Error: Couldn't send JOB message\n");
            return 1;
        }

        if (recv(socket_desc, serverreply, sizeof(serverreply) - 1, 0) < 0) {
            printf("Error: Couldn't receive job\n");
            return 1;
        }

        serverreply[sizeof(serverreply) - 1] = 0;

        char *job = strtok(serverreply, ",");
        char *work = strtok(NULL, ",");
        char *diff = strtok(NULL, "");
        if (job == NULL || work == NULL || diff == NULL) {
            printf("Malformed job received: %s\n", serverreply);
            return 1;
        }

        char ducos1_result_string[32] = "";
        time(&start_t);
        long range = (100 * atol(diff)) + 1;

        for (long i = 0; i < range; i++) {
            char str_to_hash[256] = "";
            strcat(str_to_hash, job);

            itoa((int)i, ducos1_result_string);
            strcat(str_to_hash, ducos1_result_string);

            unsigned char temp[SHA_DIGEST_LENGTH];
            char buf[SHA_DIGEST_LENGTH * 2 + 1];
            memset(buf, 0x0, sizeof(buf));
            memset(temp, 0x0, sizeof(temp));
            SHA1((unsigned char *)str_to_hash, strlen(str_to_hash), temp);

            for (long iZ = 0; iZ < SHA_DIGEST_LENGTH; iZ++)
                sprintf((char *)&(buf[iZ * 2]), "%02x", temp[iZ]);

            if (strcmp(work, buf) == 0) {
                time(&end_t);
                diff_t = difftime(end_t, start_t);
                if (diff_t <= 0.0) diff_t = 1.0;
                hashrate = (atoi(ducos1_result_string) / diff_t) / 1000;

                if (send(socket_desc, ducos1_result_string, strlen(ducos1_result_string), 0) < 0) {
                    printf("Error: Couldn't send result\n");
                    return 1;
                }

                char feedback[16] = "";
                if (recv(socket_desc, feedback, sizeof(feedback) - 1, 0) < 0) {
                    printf("Error: Feedback couldn't be received\n");
                    return 1;
                }

                if (strcmp("GOOD\n", feedback) == 0 || strcmp("BLOCK\n", feedback) == 0) {
                    accepted_shares++;
                    printf("Accepted share #%u (%s) %u kH/s\n", accepted_shares, ducos1_result_string, hashrate);
                } else if (strcmp("INVU\n", feedback) == 0) {
                    printf("Error: Incorrect username or miner key\n");
                    return 1;
                } else {
                    rejected_shares++;
                    printf("Rejected share #%u (%s) %u kH/s\n", rejected_shares, ducos1_result_string, hashrate);
                }
                break;
            }
        }
    }
    return 0;
}

