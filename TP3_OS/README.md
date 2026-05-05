NOM: EL MAHDI SABIR, FILAHI

# Projet BICEPS v4.0

Ce projet implémente un shell interactif avec des capacités réseau via BEUIP. 
Il contient un système de gestion des commandes séquentielles, des pipelines et des redirections standard, 
ainsi qu'un réseau peer-to-peer basé sur UDP et TCP.

## Organisation du code

Le code source a été redécoupé en différents modules pour garantir une séparation claire des responsabilités :
- **biceps.c** : Gère la boucle principale `readline` et les commandes internes (built-ins).
- **gescom.c / .h** : Contient le parseur basé sur `strtok_r` et le moteur d'exécution (fork, pipe, dup2). Les commandes sont analysées itérativement.
- **creme.c / .h** : Implémente la stack réseau. Un thread UDP gère la découverte asynchrone et les messages. Un thread TCP gère les requêtes `ls` et `cat` via fork.

## Lancement

La compilation s'effectue via un `Makefile` standardisé :
```bash
make
./biceps
```

Pour compiler avec le mode debug (tracing réseau et allocation mémoire) :
```bash
make memory-leak
valgrind --leak-check=full ./biceps-memleak
```

## Fonctionnalités principales

1. **Analyse syntaxique et exécution**
   - Pipeling jusqu'à 32 commandes.
   - Redirection de flots standard `<`, `>`, `>>`, `2>`, `2>>`.
   - Historisation via `readline`.

2. **Protocole de communication BEUIP (Port UDP 9998)**
   - Découverte des hôtes avec `getifaddrs` et envoi de signaux de vie (`1`, `2`, `0`).
   - Gestion fine des trames corrompues ou étrangères (Traces activables via `TRACE1` et `TRACE2`).

3. **Partage des fichiers (Port TCP 9998)**
   - Protection de l'existence des fichiers avant connexion (`access`) et contre le path traversal (../).
   - Support `ls` et téléchargement de bout en bout avec structuration dans `reppub/<pseudo>/`.
