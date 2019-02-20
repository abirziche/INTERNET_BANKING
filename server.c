#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define MAX_CLIENTS	100
#define BUFLEN 256

typedef struct Datas {
	char Nume[13];
	char Prenume[13];
	int card_number;
	int pin;
	char password[9];
	double sold;
	bool is_logged;
    bool is_blocked;
    bool confirm;
    double transfer;
    int transerTo;
} Data;

typedef struct ClInfos {
    int pos;
    int wrongCN;
    int nr_errors;
} ClInfo;

void error(char *msg)
{
    perror(msg);
    exit(1);
}

int findCN(int card_number, Data *cards, int N) {
	for (int i = 0; i < N; i++) {
		if (card_number == cards[i].card_number)
			return i;
	}
	return N;
}

int main(int argc, char *argv[])
{
    int sock_tcp, sock_udp, newsock_tcp, fdmax;
    char buffer[BUFLEN], secret_password[9], *str;
    struct sockaddr_in serv_addr, client_addr;
    int N, i, res, pass, pos, npos;
    double sum;
    socklen_t len = sizeof(serv_addr);

    if (argc != 3) {
        fprintf(stderr,"Usage : %s port_server users_data_file\n", argv[0]);
        exit(1);
    }

    // Citesc din fisierul de date si populez vectorul de structuri
    FILE *f;
    f = fopen(argv[2], "rb");
    fscanf(f, "%d", &N);
    Data cards[N];
    for (i = 0; i < N; i++) {
    	fscanf(f, "%s %s %d %d %s %lf", cards[i].Nume, cards[i].Prenume,
        &cards[i].card_number, &cards[i].pin, cards[i].password, &cards[i].sold);
    	cards[i].is_logged = false;
        cards[i].is_blocked = false;
        cards[i].confirm = false;
        cards[i].transfer = 0;
    }
    fclose(f);

    // Initializari pentru fiecare posibil client
    ClInfo pclient[MAX_CLIENTS];
    for (i = 0; i < MAX_CLIENTS; i++) {
        pclient[i].pos = N; // cand nu este loggat pe niciun card
        pclient[i].wrongCN = -1;
        pclient[i].nr_errors = 0;
    }

    // Creez socket-ul de TCP
    sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_tcp < 0) 
       error("-10 : Eroare la apel socket");

    // Creez socket-ul de UDP
   	sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_udp < 0) 
       error("-10 : Eroare la apel socket");

   	serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(atoi(argv[1]));

    if (bind(sock_tcp, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0) 
        error("-10 : Eroare la apel bind");

    if (bind(sock_udp, (struct sockaddr *) &serv_addr, sizeof(struct sockaddr)) < 0)
        error("-10 : Eroare la apel bind");

    listen(sock_tcp, MAX_CLIENTS);

    fd_set read_fds, tmp_fds;
    FD_SET(0, &read_fds);
    FD_SET(sock_tcp, &read_fds);
    FD_SET(sock_udp, &read_fds);
    fdmax = sock_udp;

    while (1) {
		tmp_fds = read_fds; 
		if (select(fdmax + 1, &tmp_fds, NULL, NULL, NULL) == -1) 
			error("-10 : Eroare la apel select");
	
		for(i = 0; i <= fdmax; i++) {
			if (FD_ISSET(i, &tmp_fds)) {

                if (i == 0) {
                    // Server-ul da quit
                    fgets(buffer, BUFLEN - 1, stdin);
                    exit(0);
                }

                if (i == sock_udp) {
                    memset(buffer, 0, BUFLEN);
                    
                    res = recvfrom(sock_udp, buffer, BUFLEN, 0,
                    (struct sockaddr *) &serv_addr, &len);
                    if (res < 0)
                       error("-10 : Eroare la apel recvfrom");
                    
                    if (strstr(buffer, "unlock")) {
                        str = strtok(buffer, " ");
                        str = strtok(NULL, " ");
                        pos = findCN(atoi(str), cards, N);
                        
                        // Primesc mesaj de forma "unlock <numar_card>"
                        if (pos != N) {
                            if (cards[pos].is_blocked == false)
                                strcpy(buffer, "UNLOCK> -6 : Operatie esuata");
                            else 
                                strcpy(buffer, "UNLOCK> Trimite parola secreta");
                        } else {
                            strcpy(buffer, "UNLOCK> -4 : Numar card inexistent");
                        }
                    } else {
                        // Primesc mesaj de forma "<numar_card> <parola_secreta>"
                        str = strtok(buffer, " ");
                        pos = findCN(atoi(str), cards, N);
                        str = strtok(NULL, " \n");
                        strcpy(secret_password, str);

                        if (strcmp(cards[pos].password, secret_password) == 0) {
                            cards[pos].is_blocked = false;
                            strcpy(buffer, "UNLOCK> Card deblocat");
                        } else {
                            strcpy(buffer, "UNLOCK> -7 : Deblocare esuata");
                        }
                    }
                    
                    res = sendto(sock_udp, buffer, BUFLEN, 0,
                    (struct sockaddr*)&serv_addr, sizeof(struct sockaddr_in));
                    if (res < 0)
                       error("-10 : Eroare la apel sendto");
                }

				if (i == sock_tcp) {
					// Un nou client vrea sa se conecteze la server
					if ((newsock_tcp = accept(sock_tcp, (struct sockaddr *) &client_addr, &len)) == -1) {
						error("-10 : Eroare la apel accept");
					} else {
						// Adaug noul socket in read_fds
						FD_SET(newsock_tcp, &read_fds);
						if (newsock_tcp > fdmax) { 
							fdmax = newsock_tcp;
						}
					}
				} else if (i != sock_udp) {
					// Primesc comenzi de la clienti
					memset(buffer, 0, BUFLEN);

					if ((res = recv(i, buffer, sizeof(buffer), 0)) <= 0) {
						if (res == 0) {
							// Clientul "i" a dat quit, deci il scoatem din read_fds
                            close(i); 
                            FD_CLR(i, &read_fds);
						} else {
							error("-10 : Eroare la apel recv");
						}
					} else {

    					if (strstr(buffer, "login")) {
                            // Primesc mesaj de forma "login <numar_card> <pin>"
                            str = strtok(buffer, " ");
    						str = strtok(NULL, " ");
    						pos = findCN(atoi(str), cards, N);
    						str = strtok(NULL, " ");
    						pass = atoi(str);

    						memset(buffer, 0, BUFLEN);
    						// Verific daca exista cardul in sistem
    						if (pos != N) {
                                // Verific daca cardul este blocat
                                if (cards[pos].is_blocked == false) {
                                    // Verific daca este cineva logat pe card
                                    if (cards[pos].is_logged == false) {
    								    // Verific daca pin-ul este cel corect
    								    if (cards[pos].pin == pass) {
                                            strcpy(buffer, "IBANK> Welcome ");
                                            strcat(buffer, cards[pos].Nume);
                                            strcat(buffer, " ");
                                            strcat(buffer, cards[pos].Prenume);
                                            cards[pos].is_logged = true;
                                            pclient[i].pos = pos;
                                            pclient[i].nr_errors = 0;
    								    } else {
                                            strcpy(buffer, "IBANK> -3 : Pin gresit");

                                            // Verific daca se greseste pin-ul de 3 ori consecutiv
                                            if (pclient[i].nr_errors == 0) {
                                                pclient[i].nr_errors++;
                                                pclient[i].wrongCN = cards[pos].card_number;
                                            } else {
                                                if (pclient[i].wrongCN == cards[pos].card_number) {
                                                    pclient[i].nr_errors++;
                                                    if (pclient[i].nr_errors == 3) {
                                                        cards[pos].is_blocked = true;
                                                        pclient[i].nr_errors = 0;
                                                        memset(buffer, 0, BUFLEN);
                                                        strcpy(buffer, "IBANK> -5 : Card Blocat");
                                                    }
                                                } else {
                                                    pclient[i].nr_errors = 1;
                                                    pclient[i].wrongCN = cards[pos].card_number; 
                                                }
                                            }
    									}
                                    } else {
    								    strcpy(buffer, "IBANK> -2 : Sesiune deja deschisa");
                                    }
                                } else {
                                    strcpy(buffer, "IBANK> -5 : Card Blocat");
                                }
    						} else {
    							strcpy(buffer, "IBANK> -4 : Numar card inexistent");
    						}
    						send(i, buffer, BUFLEN, 0);
    					} 
                        else if (strstr(buffer, "logout")) {
                            memset(buffer, 0, BUFLEN);
    						strcpy(buffer, "IBANK> Clientul a fost deconectat");
    						send(i, buffer, BUFLEN, 0);
                            pos = pclient[i].pos;
    						cards[pos].is_logged = false;
    					}
                        else if (strstr(buffer, "listsold")) {
                            memset(buffer, 0, BUFLEN);
                            strcpy(buffer, "IBANK> ");
                            pos = pclient[i].pos;
                            char tmp[34];
                            sprintf(tmp, "%.2f", cards[pos].sold);
                            strcat(buffer, tmp);
                            send(i, buffer, BUFLEN, 0);
                        }
                        else if (strstr(buffer, "transfer")) {
                            // Primesc mesaj de forma "transfer <numar_card> <suma>"
                            str = strtok(buffer, " ");
                            str = strtok(NULL, " ");
                            pos = findCN(atoi(str), cards, N);
                            str = strtok(NULL, " \n");
                            char help[20];
                            strcpy(help, str);
                            sum = atof(str);

                            memset(buffer, 0, BUFLEN);
                            // Verific daca numarul de card al destinatarului se afla in sistem
                            if (pos != N) {
                                npos = pclient[i].pos;

                                // Verific daca sunt suficiente fonduri pentru un transfer
                                if (cards[npos].sold >= sum) {
                                    strcpy(buffer, "IBANK> Transfer ");
                                    strcat(buffer, help);
                                    strcat(buffer, " catre ");
                                    strcat(buffer, cards[npos].Nume);
                                    strcat(buffer, " ");
                                    strcat(buffer, cards[npos].Prenume);
                                    strcat(buffer, "? [y/n]");
                                    cards[npos].confirm = true;
                                    cards[npos].transfer = sum;
                                    cards[npos].transerTo = pos;
                                } else {
                                    strcpy(buffer, "IBANK> -8 : Fonduri insuficiente");
                                }
                            } else {
                                strcpy(buffer, "IBANK> -4 : Numar card inexistent");
                            }
                            send(i, buffer, BUFLEN, 0);
                        }
                        else if (strstr(buffer, "quit")) {
                            pclient[i].pos = N;
                            pclient[i].wrongCN = -1;
                            pclient[i].nr_errors = 0;
                            npos = pclient[i].pos;
                            cards[npos].is_logged = false;
                        } else {
                            // Verific daca raspunsul dat de client este pozitiv/negativ
                            npos = pclient[i].pos;
                            if (cards[npos].confirm == true) {
                                if (buffer[0] == 'y') {
                                    cards[cards[npos].transerTo].sold += cards[npos].transfer; 
                                    cards[npos].sold -= cards[npos].transfer;
                                    cards[npos].transfer = 0;
                                    cards[npos].confirm = false;
                                    memset(buffer, 0, BUFLEN);
                                    strcpy(buffer, "IBANK> Transfer realizat cu succes");
                                } else {
                                    memset(buffer, 0, BUFLEN);
                                    strcpy(buffer, "IBANK> -9 : Operatie anulata");
                                }
                            } else {
                                memset(buffer, 0, BUFLEN);
                                strcpy(buffer, "IBANK> -6 : Operatie esuata");
                            }
                            send(i, buffer, BUFLEN, 0);
                        }
					}
				}
			}
		}
	}

	close(sock_tcp);
    close(sock_udp);

	return 0;
}
