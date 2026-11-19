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
