/*** includes ***/

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/*** defines ***/

#define KILO_VERSION "0.0.1"

// 0x1f == 00011111; bitmask na tecla apertada, igualando o que o CTRL faz.
// q == 113 (01110001), CTRL+q == 17 (00010001).
#define CTRL_KEY(k) ((k) & 0x1f)

enum editorKey {
  ARROW_LEFT = 1000,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};

/*** data ***/

// struct global para as confirações do editor.
struct editorConfig {
  int cx, cy; // Posição do cursor.
  int screenrows;
  int screencols;
  struct termios orig_termios;
};

struct editorConfig E;

// Configurações terminal como variável global para ser acessível sem ser
// passada por parametros nas funções.
struct termios orig_termios;

/*** terminal ***/

void die(const char *s) {
  write(STDOUT_FILENO, "\x1b[2J", 4);
  write(STDOUT_FILENO, "\x1b[H", 3);

  perror(s);
  exit(1);
}

void disableRawMode() {
  // Desativa reverte alterações na configuração do terminal.
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1) {
    die("tcsetattr");
  };
}

void enableRawMode() {
  // Termiais iniciam no "canonical mode" fazendo com que todo input do teclado
  // é enviado após apertar <Enter>. Essa função ativa o "raw mode" do terminal,
  // tornando-o ideal para um editor.

  // Cópia das configurações originais do Terminal para edição.
  if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1) {
    die("tcsetattr");
  };
  atexit(disableRawMode);

  // Cópia.
  struct termios raw = E.orig_termios;
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

int editorReadKey() {
  // Consumo dos inputs do usuário pela função read do unistd.h conjuntamente
  // com um while loop. Forma um "event loop" aguardando ação do usuário.
  int nread;
  char c;
  while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
    // Checagem do EAGAIN para caso específico no CYGWIN.
    if (nread == -1 && errno != EAGAIN) {
      die("read");
    }
  }

  // Navegação com setas.
  if (c == '\x1b') {
    char seq[3];
    // Se for um inicio de "escape sequence", ler os bytes seguintes
    if (read(STDIN_FILENO, &seq[0], 1) != 1) {
      return '\x1b';
    }
    if (read(STDIN_FILENO, &seq[1], 1) != 1) {
      return '\x1b';
    }

    if (seq[0] == '[') {
      if (seq[1] >= '0' && seq[1] <= '9') {
        if (read(STDIN_FILENO, &seq[2], 1) != 1)
          return '\x1b';
        if (seq[2] == '~') {
          switch (seq[1]) {
          case '1':
            return HOME_KEY;
          case '3':
            return DEL_KEY;
          case '4':
            return END_KEY;
          case '5':
            return PAGE_UP;
          case '6':
            return PAGE_DOWN;
          case '7':
            return HOME_KEY;
          case '8':
            return END_KEY;
          }
        }
      } else {
        switch (seq[1]) {
        case 'A':
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    } else if (seq[0] == 'O') {
      switch (seq[1]) {
      case 'H':
        return HOME_KEY;
      case 'F':
        return END_KEY;
      }
    }

    return '\x1b';
  } else {
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) {
    return -1;
  }

  while (i < sizeof(buf) - 1) {
    if (read(STDIN_FILENO, &buf[i], 1) != 1) {
      break;
    }

    if (buf[i] == 'R') {
      break;
    }
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != '\x1b' || buf[1] != '[') {
    return -1;
  }
  if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) {
    return -1;
  }

  return 0;
}

int getWindowSize(int *rows, int *cols) {
  // Usa componentes do <sys/ioctl> (input/output control) para obter tamanho do
  // terminal.
  struct winsize ws;

  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) {
      return -1;
    }
    return getCursorPosition(rows, cols);
  } else {
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}

/*** append buffer ***/

struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT                                                              \
  { NULL, 0 }

void abAppend(struct abuf *ab, const char *s, int len) {
  // Cria um buffer para juntar strings e atualizar o STDOUT de uma só vez.

  // Novo espaço na memória para comportar junção de strings.
  char *new = realloc(ab->b, ab->len + len);

  if (new == NULL) {
    return;
  }

  // Copia string fornecida no final da nova memória.
  memcpy(&new[ab->len], s, len);
  // atualiza o buffer.
  ab->b = new;
  ab->len += len;
}

void abFree(struct abuf *ab) { free(ab->b); }

/*** output ***/

void editorDrawRows(struct abuf *ab) {
  // Printa os til (~) que indicam as linhas.
  int y;
  for (y = 0; y < E.screenrows; y++) {
    // Mensagem de boas-vindas.
    if (y == E.screenrows / 3) {
      char welcome[80];
      // Interpolação de string.
      int welcomelen = snprintf(welcome, sizeof(welcome),
                                "Kilo editor -- version %s", KILO_VERSION);
      // Se tamanho do terminal for menor que mensagem, escrever apenas parte da
      // mensagem.
      if (welcomelen > E.screencols) {
        welcomelen = E.screencols;
      }

      // Centralização da mensagem.
      int padding = (E.screencols - welcomelen) / 2;
      if (padding) {
        abAppend(ab, "~", 1);
        padding--;
      }

      while (padding--) {
        abAppend(ab, " ", 1);
      }

      abAppend(ab, welcome, welcomelen);

    } else {
      abAppend(ab, "~", 1);
    }

    abAppend(ab, "\x1b[K", 3);
    if (y < E.screenrows - 1) {
      abAppend(ab, "\r\n", 2);
    }
  }
  // sequencia de chamadas a "write" substituida por um buffer.
  /***
  int y;
  for (y = 0; y < E.screenrows; y++) {
    write(STDOUT_FILENO, "~", 1);

    if (y < E.screenrows - 1) {
      write(STDOUT_FILENO, "\r\n", 2);
    }
  }***/
}

void editorRefreshScreen() {
  // Função utilza "escape sequence", que são comandos que instruem o terminal a
  // fazer algo. \x1b == 00011011 == 27 (base 10).
  struct abuf ab = ABUF_INIT;

  // Oculta o cursor
  abAppend(&ab, "\x1b[?25l", 6);

  // Reposiciona o cursor
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  // Posiciona o cursor.
  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1, E.cx + 1);
  abAppend(&ab, buf, strlen(buf));

  // Revela o cursor
  abAppend(&ab, "\x1b[?25h", 6);

  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

/*** input ***/

void editorMoveCursor(int key) {
  switch (key) {
  case ARROW_LEFT:
    if (E.cx != 0) {
      E.cx--;
    }
    break;
  case ARROW_RIGHT:
    if (E.cx != E.screencols - 1) {
      E.cx++;
    }
    break;
  case ARROW_UP:
    if (E.cy != 0) {
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if (E.cy != E.screenrows - 1) {
      E.cy++;
    }
    break;
  }
}

void initEditor() {
  E.cx = 0;
  E.cy = 0;

  if (getWindowSize(&E.screenrows, &E.screencols) == -1) {
    die("getWindowSize");
  }
}

void editorProcessKeypress() {
  int c = editorReadKey();

  switch (c) {
  case CTRL_KEY('q'):
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    exit(0);
    break;

  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    E.cx = E.screencols - 1;
    break;

  case PAGE_UP:
  case PAGE_DOWN: {
    int times = E.screenrows;
    while (times--)
      editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
  } break;

  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;
  }
}

/*** init ***/

int main() {
  enableRawMode();
  initEditor();
  while (1) {
    editorRefreshScreen();
    editorProcessKeypress();
  };
  return 0;
}
