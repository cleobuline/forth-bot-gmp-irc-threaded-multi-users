# ircforth - IRC Forth Bot

ircforth est un bot IRC écrit en C qui interprète des commandes Forth envoyées via un canal IRC. Il prend en charge une variété de commandes Forth, y compris les opérations mathématiques, la manipulation de la pile, la génération d’images, et plus encore. Conçu pour être robuste et performant, Dalle utilise une architecture multi-threadée et une gestion moderne des connexions réseau avec `getaddrinfo`.

## Usage

nohup ./ircforth name_of_irc_server bot_name "#channel"

## compilation

gcc -o ircforth -O3 -Wall -flto main.c memory_forth.c executeinstruction.c compiletoken.c images.c dictionnary.c env.c utils.c irc.c interpret.c -lgmp -lcurl -pthread 

Vérifiez que GCC est installé :
```bash
sudo apt-get install gcc

### Dépendances
- **GMP (GNU Multiple Precision Arithmetic Library)** : Pour les calculs sur grands nombres.
  - Ubuntu : `sudo apt-get install libgmp-dev`
- **libcurl** : Pour les requêtes HTTP (ex. ImgBB).
  - Ubuntu : `sudo apt-get install libcurl4-openssl-dev`
- **Bibliothèques standard C** : Inclues dans `libc6-dev` (généralement déjà installé).
  - Ubuntu : `sudo apt-get install libc6-dev`
  - 
 
## Fonctionnalités

- **Interpréteur Forth** : Exécute des commandes Forth comme `WORDS`, `5 3 +`, `NUM-TO-BIN`, ou `" monster" IMAGE ( need a space after " ) .
- **Connexion IRC** : Se connecte à un serveur IRC (par défaut `labynet.fr`) et rejoint un canal (par défaut `#labynet`).
- **Support multi-utilisateur** : Chaque utilisateur a son propre environnement Forth indépendant.
- multi-threading un threat est initié pour chaque utilisateur ainsi les appels sont non-blocants
- **Génération d’images** : Crée et héberge des images via ImgBB avec la commande `IMAGE`.
- **Résolution réseau moderne** : Utilise `getaddrinfo` pour une compatibilité IPv4/IPv6.

 
