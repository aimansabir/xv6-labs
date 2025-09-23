#include "kernel/types.h"
#include "user/user.h"
#include "kernel/fcntl.h"

void memdump(char *fmt, char *data);

int
main(int argc, char *argv[])
{
  if(argc == 1){
    printf("Example 1:\n");
    int a[2] = { 61810, 2025 };
    memdump("ii", (char*) a);
    
    printf("Example 2:\n");
    memdump("S", "a string");
    
    printf("Example 3:\n");
    char *s = "another";
    memdump("s", (char *) &s);

    struct sss {
      char *ptr;
      int num1;
      short num2;
      char byte;
      char bytes[8];
    } example;
    
    example.ptr = "hello";
    example.num1 = 1819438967;
    example.num2 = 100;
    example.byte = 'z';
    strcpy(example.bytes, "xyzzy");
    
    printf("Example 4:\n");
    memdump("pihcS", (char*) &example);
    
    printf("Example 5:\n");
    memdump("sccccc", (char*) &example);
  } else if(argc == 2){
    // format in argv[1], up to 512 bytes of data from standard input.
    char data[512];
    int n = 0;
    memset(data, '\0', sizeof(data));
    while(n < sizeof(data)){
      int nn = read(0, data + n, sizeof(data) - n);
      if(nn <= 0)
        break;
      n += nn;
    }
    memdump(argv[1], data);
  } else {
    printf("Usage: memdump [format]\n");
    exit(1);
  }
  exit(0);
}

void
memdump(char *fmt, char *data)
{
  // Your code here.
  unsigned char *d = (unsigned char *)data;

  for (char *f = fmt; *f; f++) {
    switch (*f) {
    case 'i': {               // 4-byte integer
      int v = *(int *)d;
      printf("%d\n", v);
      d += 4;
      break;
    }
    case 'p': {               // 8-byte integer in hex
      uint64 v = *(uint64 *)d;
      printf("%lx\n", (unsigned long)v);
      d += 8;
      break;
    }
    case 'h': {               // 2-byte short
      short v = *(short *)d;
      printf("%d\n", v);
      d += 2;
      break;
    }
    case 'c': {               // 1-byte character
      char ch = *(char *)d;
      printf("%c\n", ch);
      d += 1;
      break;
    }
    case 's': {               // next 8 bytes = pointer to string
      char *ptr = *(char **)d;
      printf("%s\n", ptr);
      d += 8;
      break;
    }
    case 'S': {               // rest of data is a null-terminated string
      printf("%s\n", (char *)d);
      return;
    }
    default:
      printf("memdump: unknown format '%c'\n", *f);
      return;
}
}
}
