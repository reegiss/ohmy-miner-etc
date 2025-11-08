# Resultados da Otimização de Performance do Kernel CUDA

## Data
8 de novembro de 2025

## Objetivo
Implementar kernel otimizado (`ethash_search_kernel_optimized`) para mitigar gargalo de memória global e maximizar ocupação das SMs.

## Implementação

### Mudanças Principais

1. **Kernel Otimizado (`ethash_search_kernel_optimized`)**
   - Processamento em lote: cada thread processa 4 nonces (`noncesPerThread=4`)
   - Shared memory alocada para header, seedHash e cache de DAG (4KB)
   - Carregamento cooperativo de dados compartilhados
   - Amortização de overhead de setup do kernel

2. **DeviceManager**
   - Adicionado suporte para alternar entre kernels via `OHMY_USE_OPTIMIZED_KERNEL=1`
   - Nova função `launch_ethash_search_optimized()` com parâmetro `noncesPerThread`
   - Ajuste de grid dimensions baseado em batching

3. **Preservação de Funcionalidade**
   - Kernel base mantido para comparação e fallback
   - Mesma lógica Ethash (keccak, FNV, DAG access, mix compression)
   - Validação de soluções idêntica

## Resultados de Performance

### Hardware de Teste
- **GPU**: NVIDIA GeForce GTX 1660 SUPER
- **Compute Capability**: 7.5
- **Memory**: 5739 MB
- **SMs**: 22

### Benchmark (15 segundos de mineração real)

| Kernel | Hash Rate | Tempo/Share (diff=2) |
|--------|-----------|---------------------|
| **BASE** (1 nonce/thread) | 6.314485 MH/s | 22.67 min |
| **OTIMIZADO** (4 nonces/thread) | 7.391079 MH/s | 19.37 min |

### Ganho de Performance
**+17.05%** de aumento no hashrate

### Análise
- O batching de 4 nonces por thread reduziu overhead de kernel launch
- Compartilhamento de header/seedHash em shared memory reduziu acessos globais
- Grid dimensions reduzido (1/4 dos blocos) mantendo mesmo throughput total
- Tempo esperado por share reduzido ~3.3 minutos

## Testes Funcionais
- ✅ Todos os 6 testes unitários passaram (ctest)
- ✅ Kernel otimizado encontra soluções válidas
- ✅ Formato de submissão Stratum preservado
- ✅ Nenhuma regressão funcional detectada

## Próximas Otimizações Possíveis

1. **Shared Memory Caching Avançado**
   - Implementar estratégia cooperativa de caching de DAG slices
   - Usar warp shuffle para broadcast de dados frequentes
   - Potencial ganho: +10-15%

2. **Texture Memory**
   - Bind DAG em texture memory para aproveitar cache L1/L2
   - Potencial ganho: +5-10%

3. **Tuning de noncesPerThread**
   - Testar 8, 16 nonces/thread
   - Balancear register pressure vs amortização
   - Usar Nsight Compute para encontrar ponto ótimo

4. **Occupancy Optimization**
   - Reduzir uso de registradores (pragma unroll adjustment)
   - Testar diferentes block sizes (128, 512 threads)
   - Potencial ganho: +5-8%

## Como Usar

### Kernel Base (padrão)
```bash
./ohmy-miner-etc --pool <pool> --wallet <wallet>
```

### Kernel Otimizado
```bash
OHMY_USE_OPTIMIZED_KERNEL=1 ./ohmy-miner-etc --pool <pool> --wallet <wallet>
```

## Conclusão
A primeira iteração de otimização (batching + shared memory básico) entregou **17% de ganho** sem comprometer correção funcional. Este é um resultado sólido que valida a abordagem e estabelece uma base para otimizações incrementais futuras.
