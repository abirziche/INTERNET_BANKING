#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>

#define BUFLEN 256

void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[]) {
	int sock_tcp, sock_udp, n, fdmax;
	struct sockaddr_in serv_addr;
	char buffer[BUFLEN], last_user_logged[10], *str;
    bool logged_in = false, blocked = false;
    socklen_t len = sizeof(serv_addr);

	if (argc != 3) {
		fprintf(stderr,"Usage %s IP_server port_server\n", argv[0]);
       	exit(0);
	}

	// Creez fisierul de log pentru acest client
	FILE *f;
	char file_log[20];
	sprintf(file_log, "client-%d.log", getpid());
	f = fopen(file_log, "wb");

	// Creez socket-ul de TCP
	sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
	if (sock_tcp < 0)
		error("-10 : Eroare la apel socket");

	// Creez socket-ul de UDP
	sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_udp < 0)
		error("-10 : Eroare la apel socket");

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[2]));
	inet_aton(argv[1], &serv_addr.sin_addr);

	if (connect(sock_tcp, (struct sockaddr*) &serv_addr, sizeof(serv_addr)) < 0)
		error("-10 : Eroare la apel connect");

	// Adaug cei 3 socketi in read_fds
	fd_set read_fds, tmp_fds;
	FD_SET(0, &read_fds);
    FD_SET(sock_tcp, &read_fds);
    FD_SET(sock_udp, &read_fds);
    fdmax = sock_udp;

	while(1) {

    	tmp_fds = read_fds;
		if (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1)
			error("-10 : Eroare la apel select");

		if (FD_ISSET(0, &tmp_fds)) {

  			// Citesc de la tastatura si scriu in fisierul de log
    		memset(buffer, 0 , BUFLEN);
    		fgets(buffer, BUFLEN - 1, stdin);
            fprintf(f, "%s", buffer);
			
    		// Verific daca trimit pe socket-ul de tcp 
    		if (strstr(buffer, "unlock") == NULL) {

    			if (strstr(buffer, "login")) {
                    char buffer1[BUFLEN];
                    strcpy(buffer1, buffer);
                    str = strtok(buffer1, " ");
                    str = strtok(NULL, " ");
                    strcpy(last_user_logged, str); 

    				if (logged_in == true) {
    					memset(buffer, 0 , BUFLEN);
    					strcpy(buffer, "-2 : Sesiune deja deschisa");
    					fprintf(f, "%s", buffer);
    					printf("%s\n", buffer);
    				} else {
    					n = send(sock_tcp, buffer, strlen(buffer), 0);
    					if (n < 0)
        					error("-10 : Eroare la apel send");
					}

    			}
    			else if (strstr(buffer, "logout") || strstr(buffer, "listsold") || strstr(buffer, "transfer")) {

    				if (logged_in == false) {
                        memset(buffer, 0, BUFLEN);
    					strcpy(buffer, "-1 : Clientul nu este autentificat");
    					fprintf(f, "%s\n", buffer);
    					printf("%s\n", buffer);
    				} else {
    					n = send(sock_tcp, buffer, strlen(buffer), 0);
    					if (n < 0)
        					error("-10 : Eroare la apel send");
        				
    				}
    			}
                else if (strstr(buffer, "quit")) {
                    n = send(sock_tcp, buffer, strlen(buffer), 0);
                    if (n < 0)
                        error("-10 : Eroare la apel send");
                    break;
                } else {
                    // Raspund cu parola secreta
                    if (blocked == true) {
                        char rez[50];
                        strcpy(rez, last_user_logged);
                        strcat(rez, " ");
                        strcat(rez, buffer);
                        strcpy(buffer, rez);
                        n = sendto(sock_udp, buffer, BUFLEN, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr_in));
                        if (n < 0)
                            error("-10 : Eroare la apel sendto");
                    } else {
                        // Confirm/refuz transferul
                        n = send(sock_tcp, buffer, strlen(buffer), 0);
                        if (n < 0)
                            error("-10 : Eroare la apel send");
                    }
                }
    		} else {
                memset(buffer, 0, BUFLEN);
                strcpy(buffer, "unlock ");
                strcat(buffer, last_user_logged);
                strcat(buffer, " ");
                n = sendto(sock_udp, buffer, BUFLEN, 0, (struct sockaddr*)&serv_addr, sizeof(struct sockaddr_in));
                if (n < 0)
                    error("-10 : Eroare la apel sendto");
            }
        }
    
        if (FD_ISSET(sock_tcp, &tmp_fds)) {

        	memset(buffer, 0, BUFLEN);
            if ((n = recv(sock_tcp, buffer, sizeof(buffer), 0)) <= 0) {
                if (n == 0) {
                    // Conexiunea s-a inchis, serverul a dat quit
                    break;
                } else {
                    error("-10 : Eroare la apel recv");
                }
            }

            if (strstr(buffer, "Welcome"))
                logged_in = true;
            if (strstr(buffer, "deconectat"))
                logged_in = false;

            // Afisez pe ecran si in fisierul de log
            fprintf(f, "%s\n", buffer);
            printf("%s\n", buffer);

        }

        if (FD_ISSET(sock_udp, &tmp_fds)) {

            memset(buffer, 0, BUFLEN);
            n = recvfrom(sock_udp, buffer, BUFLEN, 0, (struct sockaddr *)&serv_addr, &len);
            if (n < 0)
                error(" -10 : Eroare la apel recvfrom");

            if (strstr(buffer, "secreta"))
                blocked = true;
            if (strstr(buffer, "deblocat"))
                blocked = false;

            // Afisez pe ecran si in fisierul de log
            fprintf(f, "%s\n", buffer);
            printf("%s\n", buffer);

        }

    }

    fclose(f);
    close(sock_tcp);
    close(sock_udp);
    
    return 0;
}
