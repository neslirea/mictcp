----- Infos concernant les V3 -----

V3.1 --> Cette version a une fenêtre égale à la longueur totale de la vidéo. 
C'est-à-dire que nous incrémentons toujours le nombre de PDU envoyés et faisons le calcul sur ce nombre qui à la fin sera égale à la longueur totale de la vidéo.

V3.2 --> Cette version a une fenêtre égale à une longueur que l'on initialiase (ici 50)
C'est-à-dire que nous incrémentons toujours le nombre de PDU envoyés (et perdus) jusqu'à ce que le nombre de pdu envoyés atteigne 50.
Une fois ce nombre atteint nous mettons les compteurs de PDU perdus et envoyés à 0 ce qui nous permet de faire des tests sur des tranches de 50 PDU.
Cependant cette version n'est pas encore optimale. Il faudrait implémenter un système de fenêtre glissante (voir V3.3)

V3.3 --> Cette version a une fenêtre égale à une longueur que l'on initialiase (ici 20)
Nous avons un tableau de longeur n contenant des 0 et des 1. Il contient les états des n derniers pdu (reçu ou perte admissible).
0 correspond à un pdu perdu, 1 à un pdu reçu.
A chaque fois qu'on devrait réemettre un pdu, on regarde si le pourcentage de bit à 0 dans le tableau est acceptable.

V4.1 --> Cette version contient la phase d'établissement de la connexion ainsi que sa fermeture, mais aussi la phase de négociation du pourcentage de perte admissible

V4.2 --> Cette version contient une gestion de l'asynchronisme.

### Commande pour compiler :
make

### Commandes pour tester MICTCP avec tsock_texte :
./tsock_texte -p
./tsock_texte -s

### Commandes pour tester MICTCP avec tsock_video :
./tsock_video -p -t mictcp
./tsock_video -s -t mictcp


### Commentaire (pour v4.1 et +) :

- Négociation :
	- pourcentage de pertes admissibles proposé par le client : 20%
	- pourcentage de pertes admissibles proposé par le serveur : 10%
	=> pourcentage de pertes admissibles retenu après négociation : 10%

- Pourcentage de pertes pour la simulation : 5%
