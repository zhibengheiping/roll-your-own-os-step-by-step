#define EFIAPI __attribute__((ms_abi))
#include <Uefi.h>
#include <Guid/QemuKernelLoaderFsMedia.h>
#include <Guid/FileInfo.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include "efi.h"

EFI_GUID gEfiSimpleFileSystemProtocolGuid = EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID;
EFI_GUID gEfiFileInfoGuid = EFI_FILE_INFO_ID;

static
struct {
  VENDOR_DEVICE_PATH          VenMediaNode;
  EFI_DEVICE_PATH_PROTOCOL    EndNode;
} QEMU_LOADER_FS_DEVICE_PATH = {
  {
    {
      MEDIA_DEVICE_PATH, MEDIA_VENDOR_DP,
      { sizeof (VENDOR_DEVICE_PATH) }
    },
    QEMU_KERNEL_LOADER_FS_MEDIA_GUID
  },
  {
    END_DEVICE_PATH_TYPE, END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL) }
  }
};

static
EFI_STATUS
get_file_info(EFI_FILE_PROTOCOL *File, EFI_FILE_INFO *info) {
  UINTN size = 0;
  EFI_STATUS status = File->GetInfo(File, &gEfiFileInfoGuid, &size, NULL);
  if (status != EFI_BUFFER_TOO_SMALL)
    return status;
  char buf[size];
  status = File->GetInfo(File, &gEfiFileInfoGuid, &size, buf);
  if (!EFI_ERROR(status))
    *info = *(EFI_FILE_INFO*)buf;
  return status;
}

static
EFI_STATUS
load_initrd(EFI_BOOT_SERVICES *BootServices, VOID **buffer) {
  EFI_DEVICE_PATH_PROTOCOL *DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)&QEMU_LOADER_FS_DEVICE_PATH;
  EFI_HANDLE Device;
  EFI_STATUS status = BootServices->LocateDevicePath(&gEfiSimpleFileSystemProtocolGuid, &DevicePath, &Device);
  if (EFI_ERROR(status))
    return status;

  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *SFSP;

  status = BootServices->HandleProtocol(Device, &gEfiSimpleFileSystemProtocolGuid, (VOID **)&SFSP);
  if (EFI_ERROR(status))
    return status;

  EFI_FILE_PROTOCOL *Root;
  status = SFSP->OpenVolume(SFSP, &Root);
  if (EFI_ERROR(status))
    return status;

  EFI_FILE_PROTOCOL *File;
  status = Root->Open(Root, &File, L"initrd", EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR(status))
    return status;

  EFI_FILE_INFO info = {0};

  status = get_file_info(File, &info);
  if (EFI_ERROR(status))
    return status;

  UINTN size = info.FileSize;
  status = BootServices->AllocatePool(EfiLoaderData, size, buffer);
  if (EFI_ERROR(status))
    return status;

  status = File->Read(File, &size, *buffer);
  return status;
}

static
EFI_STATUS
load_memory_map(EFI_BOOT_SERVICES *BootServices, UINTN *key) {
  UINTN size = 0;
  void *map = NULL;
  UINTN desc_size;
  UINT32 desc_ver;

  EFI_STATUS status = BootServices->GetMemoryMap(&size, NULL, key, &desc_size, &desc_ver);
  if (status != EFI_BUFFER_TOO_SMALL)
    return status;

  size += 2 * desc_size;

  status = BootServices->AllocatePool(EfiLoaderData, size, &map);
  if (EFI_ERROR(status))
    return status;

  status = BootServices->GetMemoryMap(&size, map, key, &desc_size, &desc_ver);
  if (EFI_ERROR(status))
    return status;

  return status;
}

static char loader_stack[2*EFI_PAGE_SIZE] __attribute__((aligned(EFI_PAGE_SIZE))) = {0};

#define PG_P 0x001
#define PG_W 0x002
#define PG_PS 0x080
#define VIRTUAL_START 0xFFFF800000000000ULL

uintptr_t pml4[EFI_PAGE_SIZE/sizeof(uintptr_t)] __attribute__((aligned(EFI_PAGE_SIZE))) = {0};
static uintptr_t pml3[EFI_PAGE_SIZE/sizeof(uintptr_t)] __attribute__((aligned(EFI_PAGE_SIZE))) = {0};

static
__attribute__((no_callee_saved_registers))
void
efi_loader_start(EFI_RUNTIME_SERVICES *RuntimeServices, void *initrd, UINTN size, void *map, UINTN desc_size) {
  struct boot_info info = {
    .stack_top = KERNEL_STACK_TOP,
    .virtual_start = VIRTUAL_START,
    .initrd = initrd,
    .nmem = 0,
  };

  EFI_MEMORY_TYPE last_type = EfiMaxMemoryType;
  EFI_PHYSICAL_ADDRESS last_end = 0;
  EFI_MEMORY_DESCRIPTOR *last_descriptor = NULL;

  for (UINTN offset=0; offset<size; offset+=desc_size) {
    EFI_MEMORY_DESCRIPTOR *p = (EFI_MEMORY_DESCRIPTOR *)(((char *)map) + offset);
    if (p->Type == EfiBootServicesCode)
      p->Type = EfiConventionalMemory;
    if (p->Type == EfiBootServicesData)
      p->Type = EfiConventionalMemory;

    if (!last_descriptor) {
      last_type = p->Type;
      last_end = p->PhysicalStart;
      last_descriptor = p;
    }

    if ((p->Type == last_type) && (p->PhysicalStart == last_end) && (p->Attribute == (last_descriptor->Attribute))) {
      last_end += EFI_PAGE_SIZE * p->NumberOfPages;
    } else {
      last_descriptor->NumberOfPages = (last_end - last_descriptor->PhysicalStart) / EFI_PAGE_SIZE;
      if (last_type == EfiConventionalMemory)
        ++info.nmem;

      last_type = p->Type;
      last_end = p->PhysicalStart + EFI_PAGE_SIZE * p->NumberOfPages;
      last_descriptor = (EFI_MEMORY_DESCRIPTOR *)(((char *)last_descriptor) + desc_size);
      if (last_descriptor != p) {
        last_descriptor->Type = p->Type;
        last_descriptor->PhysicalStart = p->PhysicalStart;
        last_descriptor->Attribute = p->Attribute;
      }
    }
  }

  if (last_type == EfiConventionalMemory)
    ++info.nmem;
  size = ((char *)last_descriptor) - ((char *)map) + desc_size;

  struct boot_mem boot_mem[info.nmem];
  info.mem = boot_mem;
  size = ((char *)last_descriptor) - ((char *)map) + desc_size;
  size_t index = 0;
  for (UINTN offset=0; (offset<size); offset+=desc_size) {
    EFI_MEMORY_DESCRIPTOR *p = (EFI_MEMORY_DESCRIPTOR *)(((char *)map) + offset);
    if (p->Type != EfiConventionalMemory)
      continue;
    boot_mem[index].base = (char *)p->VirtualStart;
    boot_mem[index].size = p->NumberOfPages * EFI_PAGE_SIZE;
    index += 1;
  }

  efi_loader_main(&info);

 exit:
  RuntimeServices->ResetSystem(EfiResetShutdown, 0, 0, NULL);
}

EFI_STATUS
EFIAPI
EfiMain(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable) {
  SystemTable->ConOut->ClearScreen(SystemTable->ConOut);

  VOID *initrd;
  EFI_STATUS status = load_initrd(SystemTable->BootServices, &initrd);
  if (EFI_ERROR(status))
    goto exit;

  UINTN size = 0;
  void *map = NULL;
  UINTN key;
  UINTN desc_size;
  UINT32 desc_ver;

  status = SystemTable->BootServices->GetMemoryMap(&size, NULL, &key, &desc_size, &desc_ver);
  if (status != EFI_BUFFER_TOO_SMALL)
    goto exit;

  size += 2 * desc_size;
  status = SystemTable->BootServices->AllocatePool(EfiLoaderData, size, &map);
  if (EFI_ERROR(status))
    goto exit;

  status = SystemTable->BootServices->GetMemoryMap(&size, map, &key, &desc_size, &desc_ver);
  if (EFI_ERROR(status))
    goto exit;

  status = SystemTable->BootServices->ExitBootServices(ImageHandle, key);
  if (EFI_ERROR(status))
    goto exit;

  for (size_t i=0; i<sizeof(pml3)/sizeof(pml3[0]); ++i)
    pml3[i] = (i<<EFI_PAGE_SHIFT)|PG_P|PG_W|PG_PS;
  for (size_t i=0; i<sizeof(pml4)/sizeof(pml4[0]); ++i)
    pml4[i] = 0;
  pml4[0] = (uintptr_t)(pml3)|PG_P|PG_W;
  pml4[sizeof(pml4)/sizeof(pml4[0])/2] = (uintptr_t)(pml3)|PG_P|PG_W;

  __asm__("mov %0, %%cr3": :"r"(pml4) : "memory");

  for (UINTN offset=0; offset<size; offset+=desc_size) {
    EFI_MEMORY_DESCRIPTOR *p = (EFI_MEMORY_DESCRIPTOR *)(((char *)map) + offset);
    p->VirtualStart = VIRTUAL_START + p->PhysicalStart;
  }

  status = SystemTable->RuntimeServices->SetVirtualAddressMap(size, desc_size, desc_ver, map);
  if (EFI_ERROR(status))
    goto exit;

  __asm__("mov %[arg5], %%r8\n"
          "mov %[stack], %%rsp\n"
          "jmp *%[func]"
          :
          : "D"(SystemTable->RuntimeServices),
            "S"(VIRTUAL_START + (uintptr_t)initrd),
            "d"(size),
            "c"(VIRTUAL_START + (uintptr_t)map),
            [arg5]"r"(desc_size),
            [stack]"r"(VIRTUAL_START + (uintptr_t)loader_stack + sizeof(loader_stack)),
            [func]"r"(VIRTUAL_START + (uintptr_t)efi_loader_start)
          : "r8");

 exit:
  SystemTable->RuntimeServices->ResetSystem(EfiResetShutdown, 0, 0, NULL);
}
