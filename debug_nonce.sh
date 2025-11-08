#!/bin/bash

# Script para debugar nonce ranges e estatísticas de mineração

echo "=== ANÁLISE DE NONCE RANGES ==="
echo "Data: $(date)"
echo

# Captura 1 minuto de log verboso para análise de nonce
echo "Iniciando captura de nonce ranges por 1 minuto..."
cd /home/regis/develop/ohmy-miner-etc/build

timeout 60s ./src/ohmy-miner-etc --pool etc.2miners.com:1010 --wallet 0x742d35Cc6731C0532925a3b8D94d30A8f33b1234 --worker test-rig-debug --verbose 2>&1 | tee /tmp/nonce_debug.log

echo
echo "=== ANÁLISE DOS NONCE RANGES ==="

# Extrai informações de nonce dos logs
echo "1. Ranges de busca utilizados:"
grep "Search #" /tmp/nonce_debug.log | head -10

echo
echo "2. Nonces iniciais:"
grep "startNonce=" /tmp/nonce_debug.log | cut -d'=' -f2 | cut -d',' -f1 | head -10

echo
echo "3. Ranges de busca:"
grep "range=" /tmp/nonce_debug.log | cut -d'=' -f3 | cut -d',' -f1 | head -10

echo
echo "4. Estatísticas de hashrate:"
grep "Hash rate:" /tmp/nonce_debug.log | tail -5

echo
echo "5. Jobs recebidos:"
grep "New mining job:" /tmp/nonce_debug.log

echo
echo "6. Targets definidos:"
grep "Target:" /tmp/nonce_debug.log | tail -3

echo
echo "=== ANÁLISE DE COBERTURA DE NONCE ==="
echo "Verificando se há gaps ou sobreposições nos ranges..."

# Extrai startNonce e range para calcular cobertura
grep "startNonce=" /tmp/nonce_debug.log | sed 's/.*startNonce=\([0-9]*\).*range=\([0-9]*\).*/\1 \2/' | head -20 > /tmp/nonce_ranges.txt

echo "Primeiros 10 ranges (startNonce, range, endNonce):"
head -10 /tmp/nonce_ranges.txt

echo
echo "Verificando gaps entre ranges consecutivos:"
awk 'NR>1 {
    if(prev_end != $1) {
        printf "GAP: %s -> %s (diferença: %s)\n", prev_end, $1, $1-prev_end;
    }
} {prev_end=$3}' /tmp/nonce_ranges.txt | head -5

echo
echo "=== COMPARAÇÃO COM OUTROS MINERS ==="
echo "Range típico por busca: 262144 (256K)"
echo "Com 6.165 MH/s, deveria cobrir 256K nonces em ~41ms"
echo "Esperado: ~24 buscas por segundo"

echo
total_searches=$(grep -c "Search #" /tmp/nonce_debug.log)
echo "Buscas realizadas em 60s: $total_searches"
echo "Taxa de busca: $((total_searches/60))/s"

echo
echo "=== RECOMENDAÇÕES ==="
if [ $total_searches -lt 1000 ]; then
    echo "⚠️  PROBLEMA: Taxa de busca muito baixa!"
    echo "   - Esperado: ~1440 buscas/min (24/s)"
    echo "   - Atual: $total_searches/min"
    echo "   - Possível causa: GPU muito lenta ou overhead de CPU"
else
    echo "✓ Taxa de busca está adequada"
fi

echo
echo "Log completo salvo em: /tmp/nonce_debug.log"
