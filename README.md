# NppSpeechPlugin

Plugin **Notepad++** de lecture vocale : il lit à voix haute la sélection ou le document entier avec les voix de Windows.

Fork de [chcg/SpeechPlugin](https://github.com/chcg/SpeechPlugin), lui-même issu du plugin d'origine de Jim Xochellis
([SourceForge](https://sourceforge.net/projects/npp-plugins/files/SpeechPlugin/)).

## Fonctionnalités

- Lecture de la **sélection** ou du **document** entier, avec pause, reprise et arrêt
- **Choix de la voix** :
  - voix SAPI « Desktop » (ex. *Microsoft Hortense Desktop*, *Microsoft Zira Desktop*)
  - voix Windows OneCore, de meilleure qualité (ex. *Microsoft Julie*, *Microsoft Paul*, *Microsoft Hortense*)
- **Vitesse de lecture** réglable de -10 à +10 (curseur, ou commandes *Lire plus vite* / *Lire plus lentement*)
- Sous-menu **clic droit** optionnel dans l'éditeur
- Menus en **français** ou en **anglais** (par défaut : la langue de Notepad++)
- Gère les documents ANSI et UTF-8, ainsi que les très gros fichiers (positions 64 bits)

La synthèse vocale est faite **localement** par Windows : aucun texte n'est envoyé sur Internet.

## Prérequis

- Windows 10 ou 11
- Notepad++ 8.6 ou plus récent (64 bits, 32 bits ou ARM64)
- Au moins une voix installée (voir [Ajouter des voix](#ajouter-des-voix))

## Installation dans Notepad++

1. Récupérez `SpeechPlugin.dll` correspondant à votre Notepad++ :
   - dans les [Releases](https://github.com/AtHerGloaguen/NppSpeechPlugin/releases) du dépôt (archives `x64`, `Win32`, `ARM64`),
   - ou en le compilant (voir [Compilation](#compilation)).

   Pour connaître la version de Notepad++ : menu **?** → **À propos de Notepad++** (indique *64-bit* ou *32-bit*).
2. **Fermez Notepad++.**
3. Créez le dossier `SpeechPlugin` dans le dossier des plugins, puis copiez-y la DLL :

   ```
   C:\Program Files\Notepad++\plugins\SpeechPlugin\SpeechPlugin.dll
   ```

   Le nom du dossier doit être identique au nom de la DLL. Les droits administrateur sont nécessaires sous `Program Files`.
4. Relancez Notepad++ : le plugin apparaît dans le menu **Modules d'extension** → **Speech**.

Pour mettre à jour, fermez Notepad++ et remplacez la DLL. Pour désinstaller, supprimez le dossier `plugins\SpeechPlugin`.

## Utilisation

Menu **Modules d'extension** → **Speech** :

| Commande | Rôle |
|---|---|
| Lire la sélection | Lit le texte sélectionné |
| Lire le document | Lit tout le document actif |
| Arrêter la lecture | Arrête la lecture |
| Pause / Reprendre la lecture | Met en pause et reprend |
| Lire plus vite / Lire plus lentement | Change la vitesse d'un cran |
| Paramètres de lecture... | Ouvre la fenêtre de réglages |

Des raccourcis clavier peuvent être associés à ces commandes via **Paramètres** → **Raccourcis clavier** → onglet **Modules d'extension**.

### Paramètres de lecture

- **Voix** : liste des voix installées. Le choix s'applique immédiatement.
- **Vitesse de lecture** : de -10 (lent) à +10 (rapide), 0 = normal. Avec une voix Desktop, le changement s'entend pendant la lecture en cours ; avec une voix OneCore, il s'applique à la lecture suivante.
- **Tester** : lit une phrase d'essai avec la voix et la vitesse choisies.
- **Ajouter au menu clic droit** : ajoute un sous-menu *Speech* au clic droit de l'éditeur (lire la sélection, lire le document, arrêter, plus vite, plus lentement, paramètres).
  Le plugin modifie pour cela le fichier `contextMenu.xml` de Notepad++ (une copie `contextMenu.xml.bak` est faite avant). **Redémarrez Notepad++** pour voir le changement.
- **Langue des menus** : Automatique (langue de Notepad++), English ou Français. Nécessite un redémarrage de Notepad++.

Les réglages sont enregistrés dans `SpeechPlugin.ini`, dans le dossier de configuration des plugins
(en général `%APPDATA%\Notepad++\plugins\config\`).

## Ajouter des voix

Dans Windows : **Paramètres** → **Heure et langue** → **Voix** (ou **Langue et région**), puis ajoutez une langue avec l'option *Synthèse vocale*.
Les voix installées apparaissent ensuite dans la liste des paramètres du plugin.

Les voix *« Online (Natural) »* d'Edge (ex. *Denise Online*) sont des voix en ligne réservées à Edge : elles ne sont pas utilisables par le plugin.

## Compilation

Prérequis : Visual Studio 2022 ou plus récent avec la charge de travail *Développement Desktop en C++* et le Windows SDK 10.

```
msbuild SpeechPlugin.vcxproj /p:Configuration=Release /p:Platform=x64
```

Plateformes : `x64`, `Win32`, `ARM64`. La DLL est produite dans `<Plateforme>\Release\SpeechPlugin.dll`.
Le projet cible le toolset `v145` (Visual Studio 2026) ; avec Visual Studio 2022, ajoutez `/p:PlatformToolset=v143`.

La CI GitHub (`.github/workflows/CI_build.yml`) compile toutes les plateformes à chaque push et publie les archives lors d'un tag.

## Licence

GPL v2 ou ultérieure, voir [LICENSE](LICENSE).
Copyright (C) 2008 Jim Xochellis, et contributeurs.
