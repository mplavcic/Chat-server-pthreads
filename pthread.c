
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <pthread.h>
#include <sys/types.h>
#include <signal.h>

#define MAX_CLIENTS 100
#define BUFFER_SZ 2048

static _Atomic unsigned int client_count = 0;
static int uid = 1;

// Client structure
typedef struct {
	struct sockaddr_in addr; // client's remote address
    	int connfd;              // connection fd
    	int uid;                 // client's unique identifier
    	char name[32];           
} client_t;

client_t *clients[MAX_CLIENTS];

pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;

char *_strdup(const char *s) {
	
	size_t size = strlen(s) + 1;
    	char *p = malloc(size);
    	
	if (p) {
		memcpy(p, s, size);
	}
	return p;
}

void queue_add(client_t *client) {
       
	pthread_mutex_lock(&clients_mutex);
       	
	for (int i = 0; i < MAX_CLIENTS; ++i) {
	       	if (!clients[i]) {
		       	clients[i] = client;
            		break;
		}
	} 
	
	pthread_mutex_unlock(&clients_mutex);
}

void queue_delete(int uid) {
	
	pthread_mutex_lock(&clients_mutex);
	
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (clients[i]->uid == uid) {
                	clients[i] = NULL;
                	break;
			}
		}
	}
	
	pthread_mutex_unlock(&clients_mutex);
}

void send_message(char *s, int uid) {
	
	pthread_mutex_lock(&clients_mutex);
    	
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (clients[i]->uid != uid) {
				if (write(clients[i]->connfd, s, strlen(s)) < 0) {
					perror("Write to descriptor failed");
                    			break;
				}
			}
		}
	}
	
	pthread_mutex_unlock(&clients_mutex);
}

void send_message_all(char *s) {
	
	pthread_mutex_lock(&clients_mutex);
    
	for (int i = 0; i <MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (write(clients[i]->connfd, s, strlen(s)) < 0) {
				perror("Write to descriptor failed");
                		break;
			}
		}
	}
	
	pthread_mutex_unlock(&clients_mutex);
}

void send_message_self(const char *s, int connfd) {
	
	if (write(connfd, s, strlen(s)) < 0) {
		perror("Write to descriptor failed");
        	exit(-1);
	}
}

void send_message_client(char *s, int uid) {
	
	pthread_mutex_lock(&clients_mutex);
	
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			if (clients[i]->uid == uid) {
				if (write(clients[i]->connfd, s, strlen(s))<0) {
					perror("Write to descriptor failed");
                    			break;
				}
			}
		}
	}
	
	pthread_mutex_unlock(&clients_mutex);
}

void send_active_clients(int connfd) {
	
	char s[64];

    	pthread_mutex_lock(&clients_mutex);
    	
	for (int i = 0; i < MAX_CLIENTS; ++i) {
		if (clients[i]) {
			sprintf(s, "<< [%d] %s\r\n", clients[i]->uid, clients[i]->name);
            		send_message_self(s, connfd);
		}
	}
	
	pthread_mutex_unlock(&clients_mutex);
}

void strip_newline(char *s) {
	
	while (*s != '\0') {
		if (*s == '\r' || *s == '\n') {
			*s = '\0';
		}
		s++;
	}
}

void print_client_addr(struct sockaddr_in addr) {
	
	printf("%d.%d.%d.%d",
        addr.sin_addr.s_addr & 0xff,
        (addr.sin_addr.s_addr & 0xff00) >> 8,
        (addr.sin_addr.s_addr & 0xff0000) >> 16,
        (addr.sin_addr.s_addr & 0xff000000) >> 24);
}

void *handle_client(void *arg) {
	
	char buffer_out[BUFFER_SZ];
    	char buffer_in[BUFFER_SZ / 2];
    	int read_len;

    	client_count++;
    	client_t *client = (client_t *)arg;

    	printf("<< accept ");
    	print_client_addr(client->addr);
    	printf(" (uid=%d)\n", client->uid);

    	sprintf(buffer_out, "<< %s has joined\r\n", client->name);
    	send_message_all(buffer_out);


    	send_message_self("<< see /help for list of commands\r\n", client->connfd);

    	// read input from client
    	while ((read_len = read(client->connfd, buffer_in, sizeof(buffer_in) - 1)) > 0) {
		buffer_in[read_len] = '\0';
        	buffer_out[0] = '\0';
        	strip_newline(buffer_in);

        	// continue if buffer is empty
        	if (!strlen(buffer_in))
			continue;

        	// chat commands
		if (buffer_in[0] == '/') {
			char *command, *param;
            		command = strtok(buffer_in," ");
            		if (!strcmp(command, "/quit")) {
				break;
			} else if (!strcmp(command, "/nick")) {
				param = strtok(NULL, " ");
				if (param) {
					char *old_name = _strdup(client->name);
					if (!old_name) {
						perror("Cannot allocate memory");
						continue;
					}
					strncpy(client->name, param, sizeof(client->name));
					client->name[sizeof(client->name)-1] = '\0';
                    			sprintf(buffer_out, "<< %s is now known as %s\r\n", old_name, client->name);
                    			free(old_name);
                    			send_message_all(buffer_out);
				} else {
					send_message_self("<< name cannot be null\r\n", client->connfd);
				}
			} else if (!strcmp(command, "/msg")) {
				param = strtok(NULL, " ");
				if (param) {
					int uid = atoi(param);
					param = strtok(NULL, " ");
					if (param) {
						sprintf(buffer_out, "[PM][%s]", client->name);
						while (param != NULL) {
							strcat(buffer_out, " ");
                            				strcat(buffer_out, param);
                            				param = strtok(NULL, " ");
						}
						strcat(buffer_out, "\r\n");
						send_message_client(buffer_out, uid);
					} else {
						send_message_self("<< message cannot be null\r\n", client->connfd);
					}
				} else {
					send_message_self("<< reference cannot be null\r\n", client->connfd);
				}
			} else if(!strcmp(command, "/list")) {
				sprintf(buffer_out, "<< clients %d\r\n", client_count);
				send_message_self(buffer_out, client->connfd);
				send_active_clients(client->connfd);
			} else if (!strcmp(command, "/help")) {
				strcat(buffer_out, "<< /quit     Quit chatroom\r\n");
				strcat(buffer_out, "<< /nick     <name> Change nickname\r\n");
				strcat(buffer_out, "<< /msg      <reference> <message> Send private message\r\n");
				strcat(buffer_out, "<< /list     Show active clients\r\n");
				strcat(buffer_out, "<< /help     Show help\r\n");
				send_message_self(buffer_out, client->connfd);
			} else {
				send_message_self("<< unknown command\r\n", client->connfd);
			}
		} else { // if not chat command , send message
			snprintf(buffer_out, sizeof(buffer_out), "[%s] %s\r\n", client->name, buffer_in);
			send_message(buffer_out, client->uid);
		}
	}
	
    	sprintf(buffer_out, "<< %s has left\r\n", client->name);
    	send_message_all(buffer_out);
    	close(client->connfd);

    	queue_delete(client->uid);
    	printf("<< quit ");
    	print_client_addr(client->addr);
    	printf(" (uid=%d)\n", client->uid);
    	free(client);
    	client_count--;
    	
	pthread_detach(pthread_self());

    	return NULL;
}

int main(int argc, char *argv[]) 
{
	int yes = 1;
	int listener = 0, connfd = 0;
    	struct sockaddr_in server_addr;
    	struct sockaddr_in client_addr;
    	pthread_t tid;

    	// setting up a listener socket manually (not using getaddrinfo())
	if ((listener = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
		perror("server: socket");
		return EXIT_FAILURE;
	}
    	server_addr.sin_family = AF_INET;
    	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    	server_addr.sin_port = htons(5000);

	// handling 'address in use' error
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
		perror("setsockopt");
            	return EXIT_FAILURE;
	}
    	
	// ignoring pipe signals
    	signal(SIGPIPE, SIG_IGN);

    	if (bind(listener, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
		perror("server: bind");
        	return EXIT_FAILURE;
	}
	
	if (listen(listener, 10) < 0) {
		perror("server: listening");
        	return EXIT_FAILURE;
	}

    	printf("***SERVER STARTED***\n");

    	while (1) {
		
		socklen_t client_len = sizeof(client_addr);
        	connfd = accept(listener, (struct sockaddr *) &client_addr, &client_len);

        	if ((client_count + 1) == MAX_CLIENTS) {
			printf("<< maximum clients\n");
            		printf("<< come back later ");
            		print_client_addr(client_addr);
            		printf("\n");
            		close(connfd);
            		continue;
		}

        	// setting up client's socket
        	client_t *client = (client_t *)malloc(sizeof(client_t));
        	client->addr = client_addr;
        	client->connfd = connfd;
        	client->uid = uid++;
        	sprintf(client->name, "%d", client->uid);

        	queue_add(client);
        	pthread_create(&tid, NULL, &handle_client, (void*)client);

	}
	
	return EXIT_SUCCESS;
}
