#include "../fsp.c"

int
main()
{
  fsp_context *ctx = fsp_create();
  if(!ctx)
    return 1;
  fsp_destroy(ctx);
  return 0;
}
