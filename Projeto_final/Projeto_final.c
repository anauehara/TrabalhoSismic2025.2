#include "msp430f5529.h"
#include "intrinsics.h"
#include "msp430f5xx_6xxgeneric.h"
#include <msp430.h>
#include <stdint.h>
#include <stdio.h>

#define LCD_ADDR 0x27

#define LCD_BACKLIGHT 0x08
#define LCD_ENABLE    0x04
#define LCD_RW        0x02
#define LCD_RS        0x01

void delay_us(unsigned int us);
void lcd_send_nibble(uint8_t nibble, uint8_t rs);
void lcd_cmd(uint8_t cmd);
void lcd_data(uint8_t data);
void lcd_init(void);
void lcd_puts(const char *str);
uint8_t i2cSend(uint8_t addr, uint8_t data);
void start_signal(void);
void decode_message();

// Funções DHT11
int dht11_read(uint8_t *temperature, uint8_t *humidity);

// Buffers
char data[5];
char received_message[40];

int main(void) {
    WDTCTL = WDTPW | WDTHOLD;

    // Pinos SDA/SCL (P3.0 / P3.1)
    P3SEL |= BIT0 | BIT1;
    P3REN |= BIT0 | BIT1;
    P3OUT |= BIT0 | BIT1;

    // Inicializa I2C
    UCB0CTL1 |= UCSWRST;
    UCB0CTL0 = UCMODE_3 | UCMST | UCSYNC;
    UCB0CTL1 = UCSSEL_1 | UCTR | UCSWRST;
    UCB0BRW = 2;
    UCB0CTL1 &= ~UCSWRST;

    // Botões
    P2DIR &= ~BIT1;
    P2REN |= BIT1;
    P2OUT |= BIT1;

    P1DIR &= ~BIT1;
    P1REN |= BIT1;
    P1OUT |= BIT1;

    // DHT11 - P2.4 como entrada inicialmente
    P2DIR &= ~BIT0;
    P2REN  &= ~BIT0;
    P2SEL &= ~BIT0;

    lcd_init();

    int bit_count;
    char checksum;
    char buffer[50];
    int tela = 0;

    for(;;){
        // Enviar sinal de start e verificar que o DHT11 respondeu de acordo
        start_signal();

        for (bit_count = 0; bit_count < 40; bit_count++){
            // Esperar a borda de subida
            while ((P2IN & BIT0) == 0);
 
            // Verificar se o pulso foi largo o suficiente para ser 1
            __delay_cycles(35);
            if ((P2IN & BIT0) != 0){
                received_message[bit_count] = 1;
            }
            else{
                received_message[bit_count] = 0;
            }
 
            // Esperar a borda de descida
            while ((P2IN & BIT0) != 0);
 
        }
 
        // Decodificar menssagem
        decode_message();
 
        // Verificar checksum
        checksum = data[0] + data[1] + data[2] + data[3];

        if ((P1IN & BIT1) == 0) { // S2 - avança
            __delay_cycles(5000);
            tela = (tela + 1) % 4;
            lcd_cmd(0x01); // limpa LCD
            while ((P1IN & BIT1) == 0);
        }

        if ((P2IN & BIT1) == 0) { // S1 - retrocede
            __delay_cycles(5000);
            tela = (tela == 0) ? 3 : tela - 1;
            lcd_cmd(0x01); // limpa LCD
            while ((P2IN & BIT1) == 0);
        }

        lcd_cmd(0x80);

        if (checksum != data[4]) {
            lcd_puts("Checksum erro");
        } else {
            if (tela == 0) {
                lcd_puts("Medicoes:");
            } else if (tela == 1) {
                sprintf(buffer, "Temp: %d C", data[2]);
                lcd_puts(buffer);
            } else if (tela == 2) {
                sprintf(buffer, "Umid: %d %%", data[0]);
                lcd_puts(buffer);
            } else if (tela == 3) {
                int c = data[2];
                int f = c*18 + 320;
                sprintf(buffer, "Temp: %d.%dF", f/10, f%10 );
                lcd_puts(buffer);
            }
        }

        __delay_cycles(200000);
    }
}


// --- Funções LCD e I2C (iguais ao seu código anterior) ---

void delay_us(unsigned int us) {
    while (us--) __delay_cycles(1);
}

void lcd_send_nibble(uint8_t nibble, uint8_t rs) {
    uint8_t data = (nibble & 0xF0) | LCD_BACKLIGHT;
    if (rs) data |= LCD_RS;

    i2cSend(LCD_ADDR, data | LCD_ENABLE);
    delay_us(50);
    i2cSend(LCD_ADDR, data);
    delay_us(50);
}

void lcd_cmd(uint8_t cmd) {
    lcd_send_nibble(cmd & 0xF0, 0);
    lcd_send_nibble((cmd << 4) & 0xF0, 0);
    __delay_cycles(2000);
}

void lcd_data(uint8_t data) {
    lcd_send_nibble(data & 0xF0, 1);
    lcd_send_nibble((data << 4) & 0xF0, 1);
    __delay_cycles(2000);
}

void lcd_puts(const char *str) {
    while (*str) lcd_data(*str++);
}

void lcd_init(void) {
    __delay_cycles(50000);
    lcd_send_nibble(0x30, 0);
    __delay_cycles(5000);
    lcd_send_nibble(0x30, 0);
    __delay_cycles(5000);
    lcd_send_nibble(0x30, 0);
    __delay_cycles(200);
    lcd_send_nibble(0x20, 0);
    __delay_cycles(200);

    lcd_cmd(0x28);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);
    __delay_cycles(5000);
}

uint8_t i2cSend(uint8_t addr, uint8_t data){
    UCB0I2CSA = addr;
    UCB0CTL1 |= UCTR | UCTXSTT;

    while (!(UCB0IFG & UCTXIFG));
    UCB0TXBUF = data;

    while (UCB0CTL1 & UCTXSTT);

    if (UCB0IFG & UCNACKIFG) {
        UCB0CTL1 |= UCTXSTP;
        while (UCB0CTL1 & UCTXSTP);
        return 1;
    }

    while (!(UCB0IFG & UCTXIFG));
    UCB0CTL1 |= UCTXSTP;
    while (UCB0CTL1 & UCTXSTP);
    return 0;
}  

void start_signal(void){
    // Configurar como saída
   
    P2DIR |= BIT0;
 
    // Iniciar com sinal no nível alto
    P2OUT |= BIT0;
 
    // Manter em nível baixo por 20 ms
    P2OUT &= ~BIT0;
    __delay_cycles(20000);
 
    // Voltar para nível alto e configurar o pino como entrada para ler o sinal do DHT
    // Não temos P2REN pois há um resistor de pull-up externo
    P2OUT |= BIT0;
    P2DIR &= ~BIT0;
 
    // Verificar a resposta do DHT11
    // Espera-se um sinal baixo por 80 us e alto por 80 us
 
    while((P2IN & BIT0) == 0){
        // Esperar parte baixa passar (incrementar para medir o tempo)
    }
 
    while((P2IN & BIT0) != 0){
        // Esperar a parte alta passar
    }
 
    // A partir daqui o DHT começa a mandar os bits
}

void decode_message(){
    int i;
    int j;
 
    // Para cada um dos cinco bytes enviados
    for (i=0; i<5; i++){
        data[i] = 0;
 
        // Para cada um dos 8 bits em um byte
        for (j=0; j<8; j++){
            // O bit mais alto vem primeiro
            data[i] |= received_message[8*i + j] << (7 - j);
        }
    }
}