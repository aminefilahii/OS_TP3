#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/wait.h>
#include "creme.h"

#define BEUIP_MAGIC "BEUIP"
#define BCAST_ADDR "192.168.88.255"

typedef struct peer_info {
    char pseudo[30];
    char ip[20];
    struct peer_info *next;
} peer_t;

static peer_t *peers_head = NULL;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static int sock_udp = -1, sock_tcp = -1;
static pthread_t tid_udp, tid_tcp;
static char my_name[30];
static volatile int active = 0;

void ajouteElt(char *pseudo, char *adip) {
    pthread_mutex_lock(&mtx);
    peer_t **p = &peers_head;
    while (*p) {
        if (strcmp((*p)->ip, adip) == 0 && strcmp((*p)->pseudo, pseudo) == 0) {
            pthread_mutex_unlock(&mtx);
            return;
        }
        if (strcmp((*p)->pseudo, pseudo) > 0) break;
        p = &((*p)->next);
    }
    
    peer_t *new_p = malloc(sizeof(peer_t));
    if (new_p) {
        snprintf(new_p->pseudo, 30, "%s", pseudo);
        snprintf(new_p->ip, 20, "%s", adip);
        new_p->next = *p;
        *p = new_p;
    }
    pthread_mutex_unlock(&mtx);
}

void supprimeElt(char *adip) {
    pthread_mutex_lock(&mtx);
    peer_t **p = &peers_head;
    while (*p) {
        if (strcmp((*p)->ip, adip) == 0) {
            peer_t *del = *p;
            *p = (*p)->next;
            free(del);
            pthread_mutex_unlock(&mtx);
            return;
        }
        p = &((*p)->next);
    }
    pthread_mutex_unlock(&mtx);
}

void listeElts(void) {
    pthread_mutex_lock(&mtx);
    for (peer_t *curr = peers_head; curr; curr = curr->next) {
        if (strcmp(curr->ip, "127.0.0.1") != 0) {
            printf("--- Contact: %s (IP: %s) ---\n", curr->pseudo, curr->ip);
        }
    }
    pthread_mutex_unlock(&mtx);
}

void commande(char octet1, char *message, char *pseudo) {
    if (sock_udp < 0) return;
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port = htons(9998);
    
    char buffer[2048];
    if (octet1 == '0') {
        snprintf(buffer, sizeof(buffer), "0BEUIP%s", my_name);
    } else if (octet1 == '4' || octet1 == '5') {
        snprintf(buffer, sizeof(buffer), "9BEUIP%s", message);
    }

    pthread_mutex_lock(&mtx);
    for (peer_t *curr = peers_head; curr; curr = curr->next) {
        if (strcmp(curr->ip, "127.0.0.1") == 0) continue;
        if (octet1 == '4' && pseudo && strcmp(curr->pseudo, pseudo) != 0) continue;
        
        dst.sin_addr.s_addr = inet_addr(curr->ip);
        sendto(sock_udp, buffer, strlen(buffer), 0, (struct sockaddr *)&dst, sizeof(dst));
    }
    pthread_mutex_unlock(&mtx);
}

static void trigger_discovery(void) {
    struct ifaddrs *if_list, *ifa;
    struct sockaddr_in bcast;
    char buffer[100];
    snprintf(buffer, sizeof(buffer), "1BEUIP%s", my_name);

    if (getifaddrs(&if_list) == 0) {
        for (ifa = if_list; ifa != NULL; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET || !ifa->ifa_broadaddr) continue;
            char h[NI_MAXHOST];
            if (getnameinfo(ifa->ifa_broadaddr, sizeof(struct sockaddr_in), h, sizeof(h), NULL, 0, NI_NUMERICHOST) == 0) {
                if (strcmp(h, "127.0.0.1") && strcmp(h, "0.0.0.0")) {
                    memset(&bcast, 0, sizeof(bcast));
                    bcast.sin_family = AF_INET;
                    bcast.sin_addr.s_addr = inet_addr(h);
                    bcast.sin_port = htons(9998);
                    sendto(sock_udp, buffer, strlen(buffer), 0, (struct sockaddr*)&bcast, sizeof(bcast));
                }
            }
        }
        freeifaddrs(if_list);
    }
    memset(&bcast, 0, sizeof(bcast));
    bcast.sin_family = AF_INET;
    bcast.sin_addr.s_addr = inet_addr(BCAST_ADDR);
    bcast.sin_port = htons(9998);
    sendto(sock_udp, buffer, strlen(buffer), 0, (struct sockaddr*)&bcast, sizeof(bcast));
}

static void* udp_worker(void* dummy) {
    struct sockaddr_in bind_addr, peer_addr;
    socklen_t plen;
    char rx[2048], tx[2048];
    int yes = 1;

    sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(sock_udp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    setsockopt(sock_udp, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));

    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = htons(9998);
    bind(sock_udp, (struct sockaddr*)&bind_addr, sizeof(bind_addr));

    trigger_discovery();
    ajouteElt(my_name, "127.0.0.1");

    while (active) {
        plen = sizeof(peer_addr);
        int n = recvfrom(sock_udp, rx, 2047, 0, (struct sockaddr*)&peer_addr, &plen);
        if (!active) break;
        if (n <= 5) continue;
        rx[n] = 0;

        if (strncmp(rx+1, BEUIP_MAGIC, 5) != 0) {
#ifdef TRACE1
            fprintf(stderr, ">> TRACE1: Trame non conforme ignoree\n");
#endif
            continue;
        }

        char code = rx[0];
        char *data = rx + 6;
        char sender_ip[20];
        inet_ntop(AF_INET, &peer_addr.sin_addr, sender_ip, sizeof(sender_ip));

        if (code == '1' || code == '2') {
#ifdef TRACE2
            fprintf(stderr, ">> TRACE2: Connexion de %s (%s)\n", data, sender_ip);
#endif
            ajouteElt(data, sender_ip);
            if (code == '1') {
                snprintf(tx, sizeof(tx), "2BEUIP%s", my_name);
                sendto(sock_udp, tx, strlen(tx), 0, (struct sockaddr*)&peer_addr, plen);
            }
        } else if (code == '0') {
#ifdef TRACE2
            fprintf(stderr, ">> TRACE2: Deconnexion de %s\n", sender_ip);
#endif
            supprimeElt(sender_ip);
        } else if (code == '9') {
            pthread_mutex_lock(&mtx);
            for (peer_t *c = peers_head; c; c = c->next) {
                if (strcmp(c->ip, sender_ip) == 0) {
                    printf("\n*** Reçu de %s : %s ***\n", c->pseudo, data);
                    break;
                }
            }
            pthread_mutex_unlock(&mtx);
        } else {
#ifdef TRACE1
            fprintf(stderr, ">> TRACE1: Code de protocole invalide (%c) de %s\n", code, sender_ip);
#endif
        }
    }
    return NULL;
}

static void* tcp_worker(void* dummy) {
    struct sockaddr_in srv;
    int yes = 1;
    sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    srv.sin_family = AF_INET;
    srv.sin_addr.s_addr = INADDR_ANY;
    srv.sin_port = htons(9998);
    bind(sock_tcp, (struct sockaddr*)&srv, sizeof(srv));
    listen(sock_tcp, 10);

    while (active) {
        struct sockaddr_in cli;
        socklen_t l = sizeof(cli);
        int cfd = accept(sock_tcp, (struct sockaddr*)&cli, &l);
        if (!active) {
            if (cfd >= 0) close(cfd);
            break;
        }
        if (cfd < 0) continue;

        char req;
        if (read(cfd, &req, 1) == 1) {
            if (req == 'L') {
                if (fork() == 0) {
                    dup2(cfd, 1); dup2(cfd, 2); close(cfd); close(sock_tcp);
                    execlp("ls", "ls", "-l", "reppub/", NULL);
                    exit(1);
                }
            } else if (req == 'F') {
                char fn[300];
                int i = 0;
                while (read(cfd, &req, 1) == 1 && req != '\n' && i < 299) fn[i++] = req;
                fn[i] = 0;

                if (strchr(fn, '/') != NULL || strstr(fn, "..") != NULL) {
                    write(cfd, "Erreur serveur : path traversal interdit\n", 41);
                } else {
                    char full[400];
                    snprintf(full, sizeof(full), "reppub/%s", fn);
                    if (access(full, R_OK) != 0) {
                        write(cfd, "Erreur serveur : fichier introuvable\n", 37);
                    } else {
                        if (fork() == 0) {
                            dup2(cfd, 1); dup2(cfd, 2); close(cfd); close(sock_tcp);
                            execlp("cat", "cat", full, NULL);
                            exit(1);
                        }
                    }
                }
            }
        }
        close(cfd);
        while(waitpid(-1, NULL, WNOHANG) > 0);
    }
    return NULL;
}

void beuip_start(const char *name) {
    if (active) return;
    strncpy(my_name, name, 29); my_name[29] = 0;
    mkdir("reppub", 0755);
    active = 1;
    pthread_create(&tid_udp, NULL, udp_worker, NULL);
    pthread_create(&tid_tcp, NULL, tcp_worker, NULL);
    usleep(100000);
}

void beuip_stop(void) {
    if (!active) return;
    commande('0', NULL, NULL);
    active = 0;
    
    if (sock_tcp >= 0) shutdown(sock_tcp, SHUT_RDWR);
    if (sock_udp >= 0) shutdown(sock_udp, SHUT_RDWR);
    
    pthread_join(tid_udp, NULL); 
    pthread_join(tid_tcp, NULL);
    if (sock_udp >= 0) close(sock_udp);
    if (sock_tcp >= 0) close(sock_tcp);
    
    pthread_mutex_lock(&mtx);
    while (peers_head) {
        peer_t *t = peers_head; peers_head = t->next; free(t);
    }
    pthread_mutex_unlock(&mtx);
}

static int open_peer_connection(const char *target, char *ip_out) {
    pthread_mutex_lock(&mtx);
    for (peer_t *c = peers_head; c; c = c->next) {
        if (strcmp(c->pseudo, target) == 0) {
            strcpy(ip_out, c->ip);
            pthread_mutex_unlock(&mtx);
            return 1;
        }
    }
    pthread_mutex_unlock(&mtx);
    return 0;
}

void beuip_ls(const char *target) {
    char ip[20];
    if (!open_peer_connection(target, ip)) {
        fprintf(stderr, "Destinataire inconnu.\n"); return;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    sin.sin_family = AF_INET; sin.sin_port = htons(9998); sin.sin_addr.s_addr = inet_addr(ip);
    if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) == 0) {
        write(s, "L", 1);
        char b[1024]; int r;
        while ((r = read(s, b, 1024)) > 0) write(1, b, r);
    }
    close(s);
}

void beuip_get(const char *target, const char *fname) {
    char ip[20];
    if (!open_peer_connection(target, ip)) {
        fprintf(stderr, "Utilisateur introuvable.\n"); return;
    }
    
    if (strchr(fname, '/') != NULL || strstr(fname, "..") != NULL) {
        fprintf(stderr, "Nom de fichier invalide.\n");
        return;
    }

    char dest_dir[300];
    snprintf(dest_dir, sizeof(dest_dir), "reppub/%s", target);
    mkdir(dest_dir, 0755);

    char loc[400]; snprintf(loc, sizeof(loc), "reppub/%s/%s", target, fname);
    if (access(loc, F_OK) == 0) {
        fprintf(stderr, "Un fichier du meme nom existe deja localement.\n"); return;
    }
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in sin;
    sin.sin_family = AF_INET; sin.sin_port = htons(9998); sin.sin_addr.s_addr = inet_addr(ip);
    if (connect(s, (struct sockaddr*)&sin, sizeof(sin)) == 0) {
        char req[350]; snprintf(req, sizeof(req), "F%s\n", fname);
        write(s, req, strlen(req));
        int fd = open(loc, O_WRONLY|O_CREAT|O_TRUNC, 0644);
        if (fd >= 0) {
            char b[1024]; int r;
            while ((r = read(s, b, 1024)) > 0) write(fd, b, r);
            close(fd);
            printf("Telechargement reussi de %s dans %s.\n", fname, dest_dir);
        }
    }
    close(s);
}
