# Changelog

## Non publié

### Nouveautés
- Prise en charge des poêles en firmware 2.26 / 2.27 (INDUO) : liaison établie, toutes les valeurs du poêle lues et réglages appliqués.
- Détection automatique du type de poêle au démarrage (firmware 2.29, 2.28 ou 2.26 / 2.27), sans réglage à faire.
- Les valeurs du poêle portent désormais des noms explicites, alignés sur les noms officiels Rika : avertissement (`statusWarning`), volets d'air (`airFlaps`, `airFlapsTarget`), compteurs d'erreurs, versions de l'écran, etc.

### Corrections
- La liaison ne redémarre plus toutes les minutes quand le poêle est en veille (le poêle n'envoie que les changements ; le pont vérifie désormais régulièrement qu'il répond).
- Délai entre deux trames réduit à 150 ms, pour une réaction plus rapide.
