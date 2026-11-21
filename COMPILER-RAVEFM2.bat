@echo off
cd /d "%~dp0"
set PYTHONUTF8=1
echo RAVE FM 2 : compilation en cours, patiente (2 a 5 minutes)...
python tools\build_ravefm.py --v15 "%~dp0..\ravefm_v3\sdk\firmware\FM-1_015.fwsc" > "%~dp0resultat-ravefm2.txt" 2>&1
echo fini, code %errorlevel% >> "%~dp0resultat-ravefm2.txt"
echo Fini. Tu peux fermer cette fenetre.
pause
