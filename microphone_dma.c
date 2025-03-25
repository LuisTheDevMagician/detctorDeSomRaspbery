#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "neopixel.c"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "pico/binary_info.h"
#include "inc/ssd1306.h"
#include "hardware/i2c.h"

// Pino e canal do microfone no ADC.
#define MIC_CHANNEL 2
#define MIC_PIN (26 + MIC_CHANNEL)

// Parâmetros e macros do ADC.
#define ADC_CLOCK_DIV 10.f
#define SAMPLES 10 // Número de amostras que serão feitas do ADC.
#define ADC_ADJUST(x) (x * 3.3f / (1 << 12u) - 1.65f) // Ajuste do valor do ADC para Volts.
#define ADC_MAX 3.3f
#define ADC_STEP (3.3f/5.f) // Intervalos de volume do microfone.

// Pino e número de LEDs da matriz de LEDs.
#define LED_PIN 7
#define LED_COUNT 25

#define abs(x) ((x < 0) ? (-x) : (x))

// Canal e configurações do DMA
uint dma_channel;
dma_channel_config dma_cfg;

// Buffer de amostras do ADC.
uint16_t adc_buffer[SAMPLES];

void sample_mic();
float mic_power();
uint8_t get_intensity(float v);

// para o display OLED pinos
const uint I2C_SDA = 14;
const uint I2C_SCL = 15;

// Função para converter volts em dBFS (Decibéis Full Scale)
float volts_to_dbfs(float volts, float vmax) {
  if (volts <= 0.0001f) return -100.0f;  // Evita log(0) e retorna -100 dBFS como mínimo
  return 20.0f * log10f(volts / vmax);   // Calcula dBFS
}

int main() {
  stdio_init_all();



  // Preparação da matriz de LEDs.
  printf("Preparando NeoPixel...");
  
  npInit(LED_PIN, LED_COUNT);

  // Preparação do ADC.
  printf("Preparando ADC...\n");

  adc_gpio_init(MIC_PIN);
  adc_init();
  adc_select_input(MIC_CHANNEL);

  adc_fifo_setup(
    true, // Habilitar FIFO
    true, // Habilitar request de dados do DMA
    1, // Threshold para ativar request DMA é 1 leitura do ADC
    false, // Não usar bit de erro
    false // Não fazer downscale das amostras para 8-bits, manter 12-bits.
  );

  adc_set_clkdiv(ADC_CLOCK_DIV);

  printf("ADC Configurado!\n\n");

  printf("Preparando DMA...");

  // Tomando posse de canal do DMA.
  dma_channel = dma_claim_unused_channel(true);

  // Configurações do DMA.
  dma_cfg = dma_channel_get_default_config(dma_channel);

  channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_16); // Tamanho da transferência é 16-bits (usamos uint16_t para armazenar valores do ADC)
  channel_config_set_read_increment(&dma_cfg, false); // Desabilita incremento do ponteiro de leitura (lemos de um único registrador)
  channel_config_set_write_increment(&dma_cfg, true); // Habilita incremento do ponteiro de escrita (escrevemos em um array/buffer)
  
  channel_config_set_dreq(&dma_cfg, DREQ_ADC); // Usamos a requisição de dados do ADC

  // Inicialização do i2c
  i2c_init(i2c1, ssd1306_i2c_clock * 1000);
  gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
  gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
  gpio_pull_up(I2C_SDA);
  gpio_pull_up(I2C_SCL);

  // Processo de inicialização completo do OLED SSD1306
  ssd1306_init();

  // Preparar área de renderização para o display (ssd1306_width pixels por ssd1306_n_pages páginas)
  struct render_area frame_area = {
      start_column : 0,
      end_column : ssd1306_width - 1,
      start_page : 0,
      end_page : ssd1306_n_pages - 1
  };

  calculate_render_area_buffer_length(&frame_area);

  // zera o display inteiro
  uint8_t ssd[ssd1306_buffer_length];
  memset(ssd, 0, ssd1306_buffer_length);
  render_on_display(ssd, &frame_area);

  // Amostragem de teste.
  printf("Amostragem de teste...\n");
  sample_mic();


  printf("Configuracoes completas!\n");

  printf("\n----\nIniciando loop...\n----\n");
  while (true) {

    // Realiza uma amostragem do microfone.
    sample_mic();

    // Pega a potência média da amostragem do microfone.
    float avg = mic_power();
    avg = 2.f * abs(ADC_ADJUST(avg)); // Ajusta para intervalo de 0 a 3.3V. (apenas magnitude, sem sinal)

    uint intensity = get_intensity(avg); // Calcula intensidade a ser mostrada na matriz de LEDs.

    // Limpa a matriz de LEDs.
    npClear();

    // A depender da intensidade do som, acende LEDs específicos.
    switch (intensity) {
      case 0:
        // Sem som ou som muito baixo: Vermelho com brilho reduzido.
        npSetLED(0, 200, 0, 0); 
        npSetLED(4, 200, 0, 0);
        npSetLED(6, 200, 0, 0);
        npSetLED(8, 200, 0, 0);
        npSetLED(12, 200, 0, 0);
        npSetLED(16, 200, 0, 0);
        npSetLED(18, 200, 0, 0);
        npSetLED(20, 200, 0, 0);
        npSetLED(24, 200, 0, 0);
        break;
      case 1:
        // Transição: Vermelho diminuindo, Verde aumentando (brilho reduzido).
        npSetLED(12, 0, 0, 200);
        break;
      case 2:
        // Primeiro anel.
        npSetLED(6, 200, 200, 0);
        npSetLED(7, 200, 200, 0);
        npSetLED(8, 200, 200, 0);
        npSetLED(11, 200, 200, 0);
        npSetLED(13, 200, 200, 0);
        npSetLED(16, 200, 200, 0);
        npSetLED(17, 200, 200, 0);
        npSetLED(18, 200, 200, 0);
        break;
      case 3:
        // terceiro anel.
        npSetLED(0, 0, 200, 0);
        npSetLED(1, 0, 200, 0);
        npSetLED(2, 0, 200, 0);
        npSetLED(3, 0, 200, 0);
        npSetLED(4, 0, 200, 0);
        npSetLED(5, 0, 200, 0);
        npSetLED(9, 0, 200, 0);
        npSetLED(10, 0, 200, 0);
        npSetLED(14, 0, 200, 0);
        npSetLED(15, 0, 200, 0);
        npSetLED(19, 0, 200, 0);
        npSetLED(20, 0, 200, 0);
        npSetLED(21, 0, 200, 0);
        npSetLED(22, 0, 200, 0);
        npSetLED(23, 0, 200, 0);
        npSetLED(24, 0, 200, 0);
        break;
      case 4:
        // Som alto: Verde com brilho reduzido.
        npSetLED(12, 0, 0, 255);

        npSetLED(6, 255, 255, 0);
        npSetLED(7, 255, 255, 0);
        npSetLED(8, 255, 255, 0);
        npSetLED(11, 255, 255, 0);
        npSetLED(13, 255, 255, 0);
        npSetLED(16, 255, 255, 0);
        npSetLED(17, 255, 255, 0);
        npSetLED(18, 255, 255, 0);

        npSetLED(0, 0, 255, 0);
        npSetLED(1, 0, 255, 0);
        npSetLED(2, 0, 255, 0);
        npSetLED(3, 0, 255, 0);
        npSetLED(4, 0, 255, 0);
        npSetLED(5, 0, 255, 0);
        npSetLED(9, 0, 255, 0);
        npSetLED(10, 0, 255, 0);
        npSetLED(14, 0, 255, 0);
        npSetLED(15, 0, 255, 0);
        npSetLED(19, 0, 255, 0);
        npSetLED(20, 0, 255, 0);
        npSetLED(21, 0, 255, 0);
        npSetLED(22, 0, 255, 0);
        npSetLED(23, 0, 255, 0);
        npSetLED(24, 0, 255, 0);
        break;
    }
    // Atualiza a matriz.
    npWrite();

     // Converte para dBFS (referência: 3.3V)
     float dbfs = volts_to_dbfs(avg, 3.3f);

     // Normaliza para 0 (silêncio) a 100 (máximo)
     float db_normalized = 100 + dbfs;  // Inverte a escala
     if (db_normalized < 0) db_normalized = 0;  // Limita mínimo

     // Limpa o display
     memset(ssd, 0, ssd1306_buffer_length);
    
     // Cria strings formatadas para exibir no OLED
     char intensity_str[20];
     char avg_str[20];
     char db_str[20];
     
     snprintf(intensity_str, sizeof(intensity_str), "Intensidade: %d", intensity);
     snprintf(avg_str, sizeof(avg_str), "Media: %.2fV", avg);
     snprintf(db_str, sizeof(db_str), "Nivel: %.0f de 100", db_normalized);  // Valor inteiro
     
     // Desenha as strings no display
     ssd1306_draw_string(ssd, 0, 0, intensity_str);  // Linha superior
     ssd1306_draw_string(ssd, 0, 8, avg_str);        // Linha inferior (8 pixels abaixo)
     ssd1306_draw_string(ssd, 0, 16, db_str);
     
     // Atualiza o display
     render_on_display(ssd, &frame_area);
 
     // Envia a intensidade e a média das leituras do ADC por serial.
     printf("%2d %8.4f\r", intensity, avg);
  }
}

/**
 * Realiza as leituras do ADC e armazena os valores no buffer.
 */
void sample_mic() {
  adc_fifo_drain();
  adc_run(false);

  dma_channel_configure(dma_channel, &dma_cfg,
      adc_buffer,
      &(adc_hw->fifo),
      SAMPLES, // Usar o novo número de amostras
      true
  );

  adc_run(true);
  dma_channel_wait_for_finish_blocking(dma_channel);
  adc_run(false);
}

/**
 * Calcula a potência média das leituras do ADC. (Valor RMS)
 */
float mic_power() {
  float avg = 0.f;

  for (uint i = 0; i < SAMPLES; ++i)
    avg += adc_buffer[i] * adc_buffer[i];
  
  avg /= SAMPLES;
  return sqrt(avg);
}

/**
 * Calcula a intensidade do volume registrado no microfone, de 0 a 4, usando a tensão.
 */
uint8_t get_intensity(float v) {
  uint count = 0;

  while ((v -= ADC_STEP/20) > 0.f)
    ++count;
  
  return count;
}
