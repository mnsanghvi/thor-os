//=======================================================================
// Copyright Baptiste Wicht 2013.
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE_1_0.txt or copy at
//  http://www.boost.org/LICENSE_1_0.txt)
//=======================================================================

#include "acpi.hpp"
#include "types.hpp"
#include "kernel_utils.hpp"
#include "timer.hpp"
#include "paging.hpp"

#include "console.hpp"

namespace {

uint32_t *SMI_CMD;
uint8_t ACPI_ENABLE;
uint8_t ACPI_DISABLE;
uint32_t *PM1a_CNT;
uint32_t *PM1b_CNT;
uint16_t SLP_TYPa;
uint16_t SLP_TYPb;
uint16_t SLP_EN;
uint16_t SCI_EN;
uint8_t PM1_CNT_LEN;

struct RSDPtr {
   uint8_t Signature[8];
   uint8_t CheckSum;
   uint8_t OemID[6];
   uint8_t Revision;
   uint32_t *RsdtAddress;
};

struct FACP {
   uint8_t Signature[4];
   uint32_t Length;
   uint8_t unneded1[40 - 8];
   uint32_t *DSDT;
   uint8_t unneded2[48 - 44];
   uint32_t *SMI_CMD;
   uint8_t ACPI_ENABLE;
   uint8_t ACPI_DISABLE;
   uint8_t unneded3[64 - 54];
   uint32_t *PM1a_CNT_BLK;
   uint32_t *PM1b_CNT_BLK;
   uint8_t unneded4[89 - 72];
   uint8_t PM1_CNT_LEN;
};

int memcmp(const void* s1, const void* s2, size_t n){
    auto p1 = static_cast<const unsigned char*>(s1);
    auto p2 = static_cast<const unsigned char*>(s2);

    while(n--){
        if( *p1 != *p2 ){
            return *p1 - *p2;
        } else {
            p1++;
            p2++;
        }
    }

    return 0;
}

// check if the given address has a valid header
unsigned int* check_rsd_ptr(unsigned int *ptr) {
   const char* sig = "RSD PTR ";
   auto rsdp = reinterpret_cast<RSDPtr*>(ptr);

   if (memcmp(sig, rsdp, 8) == 0){
       uint8_t check = 0;

      // check checksum rsdpd
      auto bptr = reinterpret_cast<uint8_t*>(ptr);
      for (size_t i=0; i<sizeof(struct RSDPtr); i++){
         check += *bptr;
         bptr++;
      }

      // found valid rsdpd
      if (check == 0) {
          if (rsdp->Revision == 0)
              k_print_line("ACPI 1");
         else
              k_print_line("ACPI 2");
         return reinterpret_cast<unsigned int *>(rsdp->RsdtAddress);
      }
   }

   return nullptr;
}

// finds the acpi header and returns the address of the rsdt
unsigned int *get_rsd_ptr(void){
   // search below the 1mb mark for RSDP signature
   for (auto addr = reinterpret_cast<unsigned int*>(0x000E0000); reinterpret_cast<uintptr_t>(addr) < 0x00100000; addr += 0x10 / sizeof(addr)){
      auto rsdp = check_rsd_ptr(addr);
      if (rsdp){
         return rsdp;
      }
   }

   //TODO Check unsigned
   // at address 0x40:0x0E is the RM segment of the ebda
   unsigned int ebda = *(reinterpret_cast<short *>(0x40E));   // get pointer
   ebda = ebda * 0x10 & 0x000FFFFF;   // transform segment into linear address

   // search Extended BIOS Data Area for the Root System Description Pointer signature
   for (auto addr = reinterpret_cast<unsigned int*>(ebda); reinterpret_cast<uintptr_t>(addr) < ebda + 1024; addr += 0x10 / sizeof(addr)){
      auto rsdp = check_rsd_ptr(addr);
      if (rsdp){
         return rsdp;
      }
   }

   return nullptr;
}

// checks for a given header and validates checksum
int check_header(unsigned int *ptr, const char* sig){
    if (memcmp(ptr, sig, 4) == 0){
        char *checkPtr = reinterpret_cast<char *>(ptr);
        int len = *(ptr + 1);
        char check = 0;

        while (0<len--){
            check += *checkPtr;
            checkPtr++;
        }

        if (check == 0){
            return 0;
        }
    }

    return -1;
}

int acpiEnable(void){
    // check if acpi is enabled
    if ( (in_word(reinterpret_cast<uint64_t>(PM1a_CNT)) &SCI_EN) == 0 ){
        // check if acpi can be enabled
        if (SMI_CMD != 0 && ACPI_ENABLE != 0){
            out_byte(reinterpret_cast<uint64_t>(SMI_CMD), ACPI_ENABLE); // send acpi enable command
            // give 3 seconds time to enable acpi
            int i;
            for (i=0; i<300; i++ ){
                if ( (in_word(reinterpret_cast<uint64_t>(PM1a_CNT)) & SCI_EN) == 1 )
                    break;
                sleep_ms(10);
            }

            if (PM1b_CNT != 0)
                for (; i<300; i++ )
                {
                    if ( (in_word(reinterpret_cast<uint64_t>(PM1b_CNT)) & SCI_EN) == 1 )
                        break;
                    sleep_ms(10);
                }
            if (i<300) {
                k_print_line("ACPI enabled");
                return 0;
            } else {
                k_print_line("Couldn't enable ACPI");
                return -1;
            }
        } else {
            k_print_line("No known way to  enable ACPI");
            return -1;
        }
    } else {
        k_print_line("ACPI was already enabled");
        return 0;
    }
}

//
// bytecode of the \_S5 object
// -----------------------------------------
//        | (optional) |    |    |    |
// NameOP | \          | _  | S  | 5  | _
// 08     | 5A         | 5F | 53 | 35 | 5F
//
// -----------------------------------------------------------------------------------------------------------
//           |           |              | ( SLP_TYPa   ) | ( SLP_TYPb   ) | ( Reserved   ) | (Reserved    )
// PackageOP | PkgLength | NumElements  | byteprefix Num | byteprefix Num | byteprefix Num | byteprefix Num
// 12        | 0A        | 04           | 0A         05  | 0A          05 | 0A         05  | 0A         05
//
//----this-structure-was-also-seen----------------------
// PackageOP | PkgLength | NumElements |
// 12        | 06        | 04          | 00 00 00 00
//
// (Pkglength bit 6-7 encode additional PkgLength bytes [shouldn't be the case here])
//
int init_acpi(){
   unsigned int *ptr = get_rsd_ptr();

   k_printf("%h\n", reinterpret_cast<uintptr_t>(ptr));

   if(!paging::identity_map(ptr, 16)){
       k_print_line("Impossible to map the ACPI tables");

       return -1;
   }

   // check if address is correct  ( if acpi is available on this pc )
   if (ptr && check_header(ptr, "RSDT") == 0){
      //k_print_line("2");
      // the RSDT contains an unknown number of pointers to acpi tables
      int entrys = *(ptr + 1);
      entrys = (entrys-36) /4;
      ptr += 36/4;   // skip header information

      while (0<entrys--){
         // check if the desired table is reached
         if (check_header(reinterpret_cast<unsigned int*>(*ptr), "FACP") == 0){
            entrys = -2;

            struct FACP *facp = reinterpret_cast<FACP*>(*ptr);
            if (check_header(reinterpret_cast<unsigned int*>(facp->DSDT), "DSDT") == 0){
               // search the \_S5 package in the DSDT
               char *S5Addr = reinterpret_cast<char *>(facp->DSDT + 36); // skip header
               int dsdtLength = *(facp->DSDT+1) -36;
               while (0 < dsdtLength--){
                  if ( memcmp(S5Addr, "_S5_", 4) == 0)
                     break;
                  S5Addr++;
               }

               // check if \_S5 was found
               if (dsdtLength > 0){
                  // check for valid AML structure
                  if ( ( *(S5Addr-1) == 0x08 || ( *(S5Addr-2) == 0x08 && *(S5Addr-1) == '\\') ) && *(S5Addr+4) == 0x12 ){
                     S5Addr += 5;
                     S5Addr += ((*S5Addr &0xC0)>>6) +2;   // calculate PkgLength size

                     if (*S5Addr == 0x0A){
                        S5Addr++;   // skip byteprefix
                     }

                     SLP_TYPa = *(S5Addr)<<10;
                     S5Addr++;

                     if (*S5Addr == 0x0A){
                        S5Addr++;   // skip byteprefix
                     }

                     SLP_TYPb = *(S5Addr)<<10;

                     SMI_CMD = facp->SMI_CMD;

                     ACPI_ENABLE = facp->ACPI_ENABLE;
                     ACPI_DISABLE = facp->ACPI_DISABLE;

                     PM1a_CNT = facp->PM1a_CNT_BLK;
                     PM1b_CNT = facp->PM1b_CNT_BLK;

                     PM1_CNT_LEN = facp->PM1_CNT_LEN;

                     SLP_EN = 1<<13;
                     SCI_EN = 1;

                     return 0;
                  } else {
                     k_print_line("\\_S5 parse error.");
                  }
               } else {
                  k_print_line("\\_S5 not present.\n");
               }
            } else {
               k_print_line("DSDT invalid.\n");
            }
         }
         ptr++;
      }
      k_print_line("no valid FACP present.\n");
   } else {
      k_print_line("no acpi.\n");
   }

   return -1;
}

} //end of anonymous namespace

void acpi::init(){
    init_acpi();
}

void acpi::shutdown(){
   // SCI_EN is set to 1 if acpi shutdown is possible
   if (SCI_EN == 0){
      return;
   }

   acpiEnable();

   // send the shutdown command
   out_word(reinterpret_cast<uint64_t>(PM1a_CNT), SLP_TYPa | SLP_EN );
   if ( PM1b_CNT != 0 ){
      out_word(reinterpret_cast<uint64_t>(PM1b_CNT), SLP_TYPb | SLP_EN );
   }

   k_print_line("acpi poweroff failed.");
}