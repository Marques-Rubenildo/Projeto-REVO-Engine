@echo off
:: Roda o cliente de teste
cd /d "%~dp0"
python scripts/test_client.py
pause
