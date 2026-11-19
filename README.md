# Native Terminal

Terminal gráfico nativo para Windows, escrito em C com Win32 API e GDI.

## Estrutura

```text
include/             Interfaces compartilhadas
src/
  console/           Backend de console e integração com processos
  interpreters/      Futuros interpretadores próprios
  terminal/          Futuras partes do motor gráfico
  main.c             Janela, buffer visual e entrada
docs/                Documentação
tests/               Testes
```

## Compilação

Com MinGW GCC:

```bash
gcc src/main.c src/console/console.c -o terminal.exe -mwindows -lgdi32 -luser32
```

## Console atual

O backend inicia o `cmd.exe` como processo filho, com entrada, saída e erro redirecionados por pipes. A janela do terminal continua sendo desenhada pelo próprio projeto; o CMD não abre uma janela de console separada.

Nesta primeira integração, o fluxo de texto funciona para comandos comuns. Aplicações que exigem um console real (por exemplo, programas interativos que manipulam diretamente o buffer ou o cursor do console) ainda não são suportadas por esse backend de pipes.

## Requisitos

- Windows 7 ou superior
- MinGW GCC

## Console event contract

The console backend communicates with the terminal through `ConsoleEvent` callbacks:

- `CONSOLE_EVENT_OUTPUT`: a byte range read from the child process output pipe.
- `CONSOLE_EVENT_CLEAR_SCREEN`: a control event reserved for backends that can detect a screen-clear operation.
- `CONSOLE_EVENT_SET_COLOR`: changes the terminal foreground and background colors.
- `CONSOLE_EVENT_EXIT`: emitted when the child process closes its output and terminates; includes its exit code.

The terminal handles these events without needing to interpret CMD commands. When the CMD process exits (for example, after the user enters `exit`), the terminal closes its window. The current redirected-pipe backend can report process termination, but it cannot automatically detect CMD's internal screen-clear operation; `cls` is still handled locally as a compatibility workaround.


## Cores

O terminal interpreta sequências ANSI SGR de cor na saída dos programas, incluindo cores normais (por exemplo, `ESC[32m`), cores brilhantes (`ESC[91m`) e restauração (`ESC[0m`). O parser mantém o estado entre blocos de leitura, inclusive quando uma sequência chega dividida.

Como o CMD está conectado por pipes e não possui um console real, o builtin `color` é reconhecido pelo terminal e convertido em `CONSOLE_EVENT_SET_COLOR`. A sintaxe `color 0A` segue o padrão do CMD: primeiro dígito para fundo e segundo para texto. `color` sem argumentos restaura as cores padrão. Outros comandos continuam sendo enviados ao CMD.
