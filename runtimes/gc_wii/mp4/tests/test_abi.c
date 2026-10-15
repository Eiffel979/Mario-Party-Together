/* The game module's C ABI from a host's point of view: load the module, look up its one export, check
 * versioning and argument validation. No dump: boots that need disc files are the dump lane's (P1-16). */
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#define LOAD(path) ((void*)LoadLibraryA(path))
#define SYM(h, name) ((void*)GetProcAddress((HMODULE)(h), name))
#else
#include <dlfcn.h>
#define LOAD(path) dlopen(path, RTLD_NOW | RTLD_LOCAL)
#define SYM(h, name) dlsym(h, name)
#endif

#include "mpt/mp4/game_abi.h"

static int failures = 0;
#define CHECK(cond)                                                     \
  do {                                                                  \
    if (!(cond)) {                                                      \
      fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
      ++failures;                                                       \
    }                                                                   \
  } while (0)

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: mpt_mp4_abi_tests <mp4_game module>\n");
    return 2;
  }
  void* module = LOAD(argv[1]);
  CHECK(module != NULL);
  if (module == NULL) return 1;
  MptMp4GetApiFn get_api = (MptMp4GetApiFn)SYM(module, "mpt_mp4_get_api");
  CHECK(get_api != NULL);
  if (get_api == NULL) return 1;

  CHECK(get_api(MPT_MP4_ABI_VERSION + 1) == NULL);
  const MptMp4Api* api = get_api(MPT_MP4_ABI_VERSION);
  CHECK(api != NULL);
  if (api == NULL) return 1;
  CHECK(api->struct_size == sizeof(MptMp4Api));
  CHECK(api->abi_version == MPT_MP4_ABI_VERSION);
  CHECK(api->build_id != NULL && strncmp(api->build_id, "mp4-", 4) == 0);

  MptMp4Game* game = (MptMp4Game*)1;
  MptMp4Error error;
  memset(&error, 0, sizeof error);
  error.struct_size = sizeof error;
  CHECK(api->create(NULL, &game, &error) == MPT_MP4_ERR_ARGUMENT);

  MptMp4BootConfig config;
  memset(&config, 0, sizeof config);
  config.struct_size = sizeof config;
  config.abi_version = MPT_MP4_ABI_VERSION + 1;
  config.disc_root = "no-such-disc";
  CHECK(api->create(&config, &game, &error) == MPT_MP4_ERR_VERSION);
  CHECK(game == NULL);

  config.abi_version = MPT_MP4_ABI_VERSION;
  CHECK(api->create(&config, &game, &error) == MPT_MP4_ERR_DISC);
  CHECK(game == NULL);
  CHECK(error.message[0] != '\0');

  if (failures == 0) printf("game module ABI: all checks passed (%s)\n", api->build_id);
  return failures == 0 ? 0 : 1;
}
