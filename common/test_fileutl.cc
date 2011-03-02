// test_fileutl.cc

#include "fileutl.h"

#include <cstdio>
#include <string>

using namespace std;

int main(int argc, char **argv)
{
  if(argc<3)
    {
      fprintf(stderr, "Sorry, not enough arguments\n");
      return -1;
    }

  if(!move_recursive(argv[1], argv[2]))
    {
      fprintf(stderr, "Move failed.\n");
      return -1;
    }
  else
    return 0;
}
