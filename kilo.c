/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/*** data ***/

// Configurações terminal como variável global para ser acessível sem ser
// passada por parametros nas funções.
struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
  perror(s);
  exit(1);
}

void disableRawMode() {
  // Desativa reverte alterações na configuração do terminal.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1) {
    die("tcsetattr");
  };
}

void enableRawMode() {
  // Termiais iniciam no "canonical mode" fazendo com que todo input do teclado
  // é enviado após apertar <Enter>. Essa função ativa o "raw mode" do terminal,
  // tornando-o ideal para um editor.

  // Cópia das configurações originais do Terminal para edição.
  if (tcgetattr(STDIN_FILENO, &orig_termios) == -1) {
    die("tcsetattr");
  };
  atexit(disableRawMode);

  // Cópia.
  struct termios raw = orig_termios;
  // ~ECHO: Desativa o print de cada input.
  // ~ICANON: leitura do input byte por byte ao invés de linha-por-linha.
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  raw.c_oflag &= ~(OPOST);
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_cflag |= ~(CS8);

  // Número minimo de bytes que read precisa para retornar.
  raw.c_cc[VMIN] = 0;
  // Tempo de espera em décimos de segundo que read tem que esperar para
  // retornar.
  raw.c_cc[VTIME] = 1;

  // Aplica as configurações no Terminal.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    die("tcsetattr");
  };
}

/*** init ***/

int main() {
  enableRawMode();
  // Consumo dos inputs do usuário pela função read do unistd.h conjuntamente
  // com um while loop. Forma um "event loop" aguardando ação do usuário.
  while (1) {
    // Caractere nulo caso read não receba input e tenha um timeout.
    char c = '\0';
    // Checagem do EAGAIN para caso específico no CYGWIN.
    if (read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) {
      die("read");
    };
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }

    if (c == 'q') {
      break;
    }
  };
  return 0;
}
