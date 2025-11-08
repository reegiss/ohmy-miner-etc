# Próxima Atividade de Desenvolvimento

## Assunto: Otimização de Performance do Kernel CUDA Ethash (`search_kernel.cu`)

### Análise
Com a correção da submissão de shares, o minerador está funcional. No entanto, o kernel de busca atual (`ethash_search_kernel` em `src/cuda/kernels/search_kernel.cu`) é uma implementação ingênua. Ele sofre de latência massiva de memória global, pois acessa o DAG diretamente do HBM (memória global) dentro do loop de `NUM_ACCESSES`. Esta é a principal barreira de performance que impede o minerador de atingir um hashrate competitivo.

Os placeholders `search_kernel_optimized` e `search_kernel_batch` no mesmo arquivo, juntamente com `docs/CUDA_OPTIMIZATION.md`, já delineiam a estratégia de otimização.

### Ação Requerida
Implemente o kernel de busca otimizado (`search_kernel_optimized`) para mitigar o gargalo de memória global e maximizar a ocupação das SMs.

### Requisitos Técnicos

1. Utilização de Memória Compartilhada (Shared Memory):
   - O kernel deve carregar cooperativamente "fatias" (slices) do DAG para a memória compartilhada (`__shared__`).
   - Os acessos ao DAG dentro do loop principal (`NUM_ACCESSES`) devem ser servidos primariamente pela memória compartilhada, não pela global.
   - Garanta que os carregamentos da memória global para a compartilhada sejam totalmente coalescidos.

2. Processamento em Lote (Batching):
   - Refatore o kernel para que cada thread processe múltiplos nonces (`noncesPerThread`).
   - Isso amortiza o custo de setup do kernel, o carregamento do header e os acessos iniciais ao DAG, melhorando significativamente a utilização da GPU.

3. Otimização de Acesso:
   - Considere o uso de memória de textura (Texture Memory) para os acessos ao DAG (cache L1/L2), conforme sugerido em `CUDA_OPTIMIZATION.md`.
   - Minimize a pressão sobre os registradores (register pressure) para permitir maior ocupação (occupancy). Monitore com ncu.

4. Atualização do Host:
   - Modifique `DeviceManager` (`src/cuda/device_manager.cu`) e a função `launch_ethash_search` para invocar o novo kernel otimizado, ajustando os parâmetros de lançamento (grid, block) e passando o `noncesPerThread`.

### Métrica de Sucesso
O novo kernel deve demonstrar um aumento significativo no hashrate (H/s) em comparação com a implementação base, medido pelo `benchmark_kernel` e por `DeviceManager::getHashRate`.

### Notas
- Priorize correção funcional primeiro; em seguida, itere em blocos de otimização (memória compartilhada, batching, textura) um por vez para medir ganho incremental.
- Utilize Nsight Compute para estimar occupancy, latências de memória e pressão de registradores.
