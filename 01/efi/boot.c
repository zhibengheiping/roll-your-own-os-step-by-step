#define EFIAPI __attribute__((ms_abi))
#include <Uefi.h>
#include "../osrt.h"

EFI_STATUS
EFIAPI
EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  SystemTable->ConOut->ClearScreen(SystemTable->ConOut);
  UINTN memory_map_size = 0, map_key;
  SystemTable->BootServices->GetMemoryMap(&memory_map_size, NULL, &map_key, NULL, NULL);
  SystemTable->BootServices->ExitBootServices(ImageHandle, map_key);
  kernel_main();
  SystemTable->RuntimeServices->ResetSystem(EfiResetShutdown, 0, 0, NULL);
}
