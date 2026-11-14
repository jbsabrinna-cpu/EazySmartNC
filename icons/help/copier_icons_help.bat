@echo off
REM Cree les sous-dossiers help par type et y copie les icones generiques.
REM A lancer depuis EazyNC\icons\help\ (la ou sont tes 8 PNG).
cd /d "%~dp0"

if not exist "fraise2t" mkdir "fraise2t"
if not exist "fraise4t" mkdir "fraise4t"
if not exist "foret" mkdir "foret"
if not exist "torique" mkdir "torique"
if not exist "boule" mkdir "boule"
if not exist "conique" mkdir "conique"
if not exist "taraud" mkdir "taraud"
if not exist "alesoir" mkdir "alesoir"
if not exist "autre" mkdir "autre"

REM Copie les 8 icones dans chaque sous-dossier
copy /Y help_*.png "fraise2t\" >nul
copy /Y help_*.png "fraise4t\" >nul
copy /Y help_*.png "foret\" >nul
copy /Y help_*.png "torique\" >nul
copy /Y help_*.png "boule\" >nul
copy /Y help_*.png "conique\" >nul
copy /Y help_*.png "taraud\" >nul
copy /Y help_*.png "alesoir\" >nul
copy /Y help_*.png "autre\" >nul

echo Termine : icones copiees dans tous les dossiers de type.
pause