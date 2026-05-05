#ifndef CREME_H
#define CREME_H

void beuip_start(const char *name);
void beuip_stop(void);
void listeElts(void);
void commande(char octet1, char *message, char *pseudo);
void beuip_ls(const char *target);
void beuip_get(const char *target, const char *fname);

#endif
