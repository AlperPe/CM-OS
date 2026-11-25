#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>

#define STAGE2_SEKTOR_SAYISI 24
#define STAGE2_BOYUT (STAGE2_SEKTOR_SAYISI * 512) // 8192 bayt (8 KB)
#define DILIM_32   100000 // Her bölge 100 KB (Toplam 500 KB havuz)
#define TABLO_32   499000 // 5 bölge * 12 bayt = 60 bayt (havuzun en sonunda)
#define UI_TABLO   499100 // 5 bölge * 20 bayt = 100 bayt (x, y, w, h, ic_renk)
#define AKTIF_ID   499200 // O an odaktaki bölge indeksi (0..4)
#define TERM_TABLO 499220 // 5 bölge * 16 bayt (imlec_x, imlec_y, bas_x, bas_y)
#define CMD_BUF    499300 // Aktif yazılan komut tamponu (128 bayt)
#define CMD_LEN    499428 // Komut uzunluğu (4 bayt)
#define FS_TABLO     480000 // 16 kayıt * 32 bayt = 512 bayt
#define FS_VERI      400000 // right bölgesi: dosya içerikleri (80 KB)
#define FS_SAYAC     480512 // Toplam dosya/dizin sayısı (4 bayt)
#define FS_VERI_SON  480516 // Sıradaki boş veri ofseti (4 bayt)
#define AKTIF_DIZIN  480520 // O anki dizin ID'si (0 = kök)
#define CME_MOD      480524 // 0 = kabuk, 1 = cme editörü aktif
#define CME_KAYIT    480528 // Düzenlenen FS_TABLO kayıt adresi
uint8_t stage1[512];
size_t pos1 = 0;

uint8_t stage2[STAGE2_BOYUT];
size_t pos2 = 0;

size_t font_fn_pos   = 0;
size_t kutu_fn_pos   = 0;
size_t char_fn_pos   = 0;
size_t prompt_fn_pos = 0;

// ============================================================================
// TEMEL YAZICILAR
// ============================================================================
void emit1_bytes(const uint8_t *arr, size_t n) {
    for (size_t i = 0; i < n && pos1 < 510; i++) stage1[pos1++] = arr[i];
}

void emit8(uint8_t b) { if (pos2 < STAGE2_BOYUT) stage2[pos2++] = b; }
void emit16(uint16_t v) { emit8(v & 0xFF); emit8((v >> 8) & 0xFF); }
void emit32(uint32_t v) {
    emit8(v & 0xFF); emit8((v >> 8) & 0xFF);
    emit8((v >> 16) & 0xFF); emit8((v >> 24) & 0xFF);
}
void emit_bytes(const uint8_t *arr, size_t n) {
    for (size_t i = 0; i < n; i++) emit8(arr[i]);
}

void emit32_yama32(size_t yama_pos) {
    uint32_t fark = (uint32_t)(pos2 - (yama_pos + 4));
    stage2[yama_pos + 0] = fark & 0xFF;
    stage2[yama_pos + 1] = (fark >> 8) & 0xFF;
    stage2[yama_pos + 2] = (fark >> 16) & 0xFF;
    stage2[yama_pos + 3] = (fark >> 24) & 0xFF;
}

// ============================================================================
// STAGE 1: 512 BAYTLIK DİSK YÜKLEYİCİ (INT 13h -> 0x7E00)
// ============================================================================
void stage1_olustur(void) {
    memset(stage1, 0, sizeof(stage1));

    uint8_t loader[] = {
        0xFA,                               // cli
        0xFC,                               // cld
        0x31, 0xC0,                         // xor ax, ax
        0x8E, 0xD8,                         // mov ds, ax
        0x8E, 0xC0,                         // mov es, ax
        0x8E, 0xD0,                         // mov ss, ax
        0xBC, 0x00, 0x7C,                   // mov sp, 0x7C00
        0xFB,                               // sti
        0xB4, 0x02,                         // mov ah, 0x02 (Sektör Oku)
        0xB0, STAGE2_SEKTOR_SAYISI,         // mov al, 16 (8 KB)
        0xB5, 0x00,                         // mov ch, 0 (Silindir 0)
        0xB1, 0x02,                         // mov cl, 2 (2. sektörden başla)
        0xB6, 0x00,                         // mov dh, 0 (Kafa 0)
        0xBB, 0x00, 0x7E,                   // mov bx, 0x7E00
        0xCD, 0x13,                         // int 0x13
        0xEA, 0x00, 0x7E, 0x00, 0x00        // jmp 0x0000:0x7E00
    };
    emit1_bytes(loader, sizeof(loader));

    stage1[510] = 0x55;
    stage1[511] = 0xAA;
}

// ============================================================================
// 16-BİT VESA VBE 1024x768x32 VE 32-BİT PROTECTED MODE GEÇİŞİ
// ============================================================================
void emit_vesa_1024x768(void) {
    // 1. VBE Controller Bilgisi Al (AX=0x4F00 -> 0x0000:0x9200)
    uint8_t vbe_init[] = {
        0x31, 0xC0,             // xor ax, ax
        0x8E, 0xD8,             // mov ds, ax
        0x8E, 0xC0,             // mov es, ax
        0xB8, 0x00, 0x4F,       // mov ax, 0x4F00
        0xBF, 0x00, 0x92,       // mov di, 0x9200
        0xCD, 0x10,             // int 0x10
        0xC4, 0x36, 0x0E, 0x92  // les si, [0x920E] (ES:SI = VideoModePtr)
    };
    emit_bytes(vbe_init, sizeof(vbe_init));

    // 2. Mod listesinde tam 1024x768x32 modunu bul
    size_t mod_dongu = pos2;
    uint8_t mod_oku[] = {
        0x26, 0x8B, 0x0C,       // mov cx, es:[si]
        0x83, 0xC6, 0x02,       // add si, 2
        0x83, 0xF9, 0xFF        // cmp cx, 0xFFFF
    };
    emit_bytes(mod_oku, sizeof(mod_oku));
    emit8(0x74); size_t je_bulunamadi = pos2; emit8(0);

    uint8_t mod_sorgu[] = {
        0x06,                   // push es
        0x56,                   // push si
        0x31, 0xC0,             // xor ax, ax
        0x8E, 0xC0,             // mov es, ax
        0xB8, 0x01, 0x4F,       // mov ax, 0x4F01
        0xBF, 0x00, 0x90,       // mov di, 0x9000 (PhysBasePtr -> 0x9028)
        0xCD, 0x10,             // int 0x10
        0x5E,                   // pop si
        0x07,                   // pop es
        0x3D, 0x4F, 0x00        // cmp ax, 0x004F
    };
    emit_bytes(mod_sorgu, sizeof(mod_sorgu));
    emit8(0x75); emit8((uint8_t)(mod_dongu - (pos2 + 1)));

    emit8(0x81); emit8(0x3E); emit16(0x9012); emit16(1024); // XResolution == 1024
    emit8(0x75); emit8((uint8_t)(mod_dongu - (pos2 + 1)));
    emit8(0x81); emit8(0x3E); emit16(0x9014); emit16(768);  // YResolution == 768
    emit8(0x75); emit8((uint8_t)(mod_dongu - (pos2 + 1)));
    emit8(0x80); emit8(0x3E); emit16(0x9019); emit8(32);    // BitsPerPixel == 32
    emit8(0x75); emit8((uint8_t)(mod_dongu - (pos2 + 1)));

    uint8_t mod_ac[] = {
        0x89, 0xCB,             // mov bx, cx
        0x81, 0xCB, 0x00, 0x40, // or bx, 0x4000 (Linear Framebuffer)
        0xB8, 0x02, 0x4F,       // mov ax, 0x4F02
        0xCD, 0x10              // int 0x10
    };
    emit_bytes(mod_ac, sizeof(mod_ac));
    stage2[je_bulunamadi] = (uint8_t)(pos2 - (je_bulunamadi + 1));
}

void emit_32bit_gecis(void) {
    uint8_t hazirlik[] = {
        0xFA,                   // cli
        0x31, 0xC0,             // xor ax, ax
        0x8E, 0xD8,             // mov ds, ax
        0xE4, 0x92,             // in al, 0x92
        0x0C, 0x02,             // or al, 2 (A20 aktif)
        0xE6, 0x92              // out 0x92, al
    };
    emit_bytes(hazirlik, sizeof(hazirlik));

    emit8(0xEB); size_t jmp_gdt_sonu = pos2; emit8(0);

    size_t gdt_bas = pos2;
    uint8_t gdt[] = {
        0x00,0x00, 0x00,0x00, 0x00,0x00, 0x00,0x00, // 0x00: Null
        0xFF,0xFF, 0x00,0x00, 0x00,0x9A, 0xCF,0x00, // 0x08: 32-bit Kod (4GB)
        0xFF,0xFF, 0x00,0x00, 0x00,0x92, 0xCF,0x00  // 0x10: 32-bit Veri (4GB)
    };
    emit_bytes(gdt, sizeof(gdt));

    size_t gdtr_bas = pos2;
    emit16(sizeof(gdt) - 1);
    emit32((uint32_t)(0x7E00 + gdt_bas));

    stage2[jmp_gdt_sonu] = (uint8_t)(pos2 - (jmp_gdt_sonu + 1));

    emit8(0x0F); emit8(0x01); emit8(0x16);         // lgdt [gdtr]
    emit16((uint16_t)(0x7E00 + gdtr_bas));

    emit8(0x0F); emit8(0x20); emit8(0xC0);         // mov eax, cr0
    emit8(0x0C); emit8(0x01);                      // or al, 1
    emit8(0x0F); emit8(0x22); emit8(0xC0);         // mov cr0, eax

    emit8(0xEA);                                   // jmp far 0x08:pm32_bas
    size_t far_jmp_hedef = pos2;
    emit16(0);
    emit16(0x0008);

    uint16_t pm32_addr = (uint16_t)(0x7E00 + pos2);
    stage2[far_jmp_hedef]     = pm32_addr & 0xFF;
    stage2[far_jmp_hedef + 1] = (pm32_addr >> 8) & 0xFF;

    uint8_t pm32_kur[] = {
        0xB8, 0x10, 0x00, 0x00, 0x00, // mov eax, 0x10
        0x8E, 0xD8,                   // mov ds, ax
        0x8E, 0xC0,                   // mov es, ax
        0x8E, 0xD0,                   // mov ss, ax
        0xBC, 0x00, 0x00, 0x09, 0x00, // mov esp, 0x00090000
        0xBD, 0x00, 0x00, 0x10, 0x00, // mov ebp, 0x00100000 (5'li Havuz = 1 MB)
        0xBF, 0x00, 0x80, 0x0B, 0x00  // mov edi, 0x000B8000
    };
    emit_bytes(pm32_kur, sizeof(pm32_kur));
}

// ============================================================================
// 32-BİT DOĞRUDAN PORT (0x64 / 0x60) KLAVYE SÜRÜCÜSÜ
// ============================================================================
void emit32_klavye_oku(void) {
    emit8(0xEB); size_t jmp_tablo = pos2; emit8(0);

    size_t tablo_pos = pos2;
    const uint8_t scancode_tablo[58] = {
        0,  0x1B, '1','2','3','4','5','6','7','8','9','0','-','=', 0x08, 0x09,
       'q','w','e','r','t','y','u','i','o','p','[',']', 0x0D, 0,
       'a','s','d','f','g','h','j','k','l',';','\'','`', 0, '\\',
       'z','x','c','v','b','n','m',',','.','/', 0, '*', 0, ' '
    };
    emit_bytes(scancode_tablo, sizeof(scancode_tablo));
    stage2[jmp_tablo] = (uint8_t)(pos2 - (jmp_tablo + 1));

    size_t bekle = pos2;
    emit8(0xE4); emit8(0x64);                         // in al, 0x64
    emit8(0xA8); emit8(0x01);                         // test al, 1
    emit8(0x74); emit8((uint8_t)(bekle - (pos2 + 1)));// jz bekle

    emit8(0xE4); emit8(0x60);                         // in al, 0x60
    emit8(0xA8); emit8(0x80);                         // test al, 0x80
    emit8(0x75); emit8((uint8_t)(bekle - (pos2 + 1)));// jnz bekle

    emit8(0x3C); emit8(58);                           // cmp al, 58
    emit8(0x73); emit8((uint8_t)(bekle - (pos2 + 1)));// jae bekle

    emit8(0x0F); emit8(0xB6); emit8(0xC0);            // movzx eax, al
    emit8(0x8A); emit8(0x80);                         // mov al, [eax + tablo]
    emit32((uint32_t)(0x7E00 + tablo_pos));

    emit8(0x84); emit8(0xC0);                         // test al, al
    emit8(0x74); emit8((uint8_t)(bekle - (pos2 + 1)));// jz bekle
}

// ============================================================================
// 5'Lİ FRAKTAL HAVUZ, >-< KENETLEME, MERKEZ TRANSFER & DİL MOTORLARI
// ============================================================================
void emit32_havuz_temizle(void) {
    uint8_t temizle[] = {
        0x57,                         // push edi
        0x89, 0xEF,                   // mov edi, ebp (0x00100000)
        0xB9, 0x48, 0xE8, 0x01, 0x00, // mov ecx, 125000 (500.000 bayt)
        0x31, 0xC0,                   // xor eax, eax
        0xF3, 0xAB,                   // rep stosd
        0x5F                          // pop edi
    };
    emit_bytes(temizle, sizeof(temizle));
}

void emit32_havuz_yaz(uint32_t ofset, uint8_t deger) {
    emit8(0xC6); emit8(0x85); emit32(ofset); emit8(deger);
}

void emit32_tablo_kur(void) {
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t ofset = TABLO_32 + i * 12;
        emit8(0xC7); emit8(0x85); emit32(ofset + 0); emit32(i * DILIM_32);
        emit8(0xC7); emit8(0x85); emit32(ofset + 4); emit32(DILIM_32);
        emit8(0xC7); emit8(0x85); emit32(ofset + 8); emit32(0);
    }
}

void emit32_kenetle(uint8_t bolge_idx, uint32_t gereken_bayt) {
    if (bolge_idx >= 4) { emit8(0x31); emit8(0xC0); return; }

    uint32_t ofset = TABLO_32 + bolge_idx * 12;
    uint32_t komsu = ofset + 12;

    emit8(0x8B); emit8(0x85); emit32(ofset + 8);
    emit8(0x05); emit32(gereken_bayt);
    emit8(0x3B); emit8(0x85); emit32(ofset + 4);
    emit8(0x76); size_t jbe_sigdi = pos2; emit8(0);

    emit8(0x83); emit8(0xBD); emit32(komsu + 8); emit8(0);
    emit8(0x75); size_t jne_dolu = pos2; emit8(0);
    emit8(0x8B); emit8(0x8D); emit32(komsu + 4);
    emit8(0x85); emit8(0xC9);
    emit8(0x74); size_t jz_dolu = pos2; emit8(0);

    emit8(0x01); emit8(0x8D); emit32(ofset + 4);
    emit8(0xC7); emit8(0x85); emit32(komsu + 4); emit32(0);
    emit8(0xB8); emit32(2);
    emit8(0xEB); size_t jmp_bit1 = pos2; emit8(0);

    stage2[jbe_sigdi] = (uint8_t)(pos2 - (jbe_sigdi + 1));
    emit8(0xB8); emit32(1);
    emit8(0xEB); size_t jmp_bit2 = pos2; emit8(0);

    stage2[jne_dolu] = (uint8_t)(pos2 - (jne_dolu + 1));
    stage2[jz_dolu]  = (uint8_t)(pos2 - (jz_dolu + 1));
    emit8(0x31); emit8(0xC0);

    stage2[jmp_bit1] = (uint8_t)(pos2 - (jmp_bit1 + 1));
    stage2[jmp_bit2] = (uint8_t)(pos2 - (jmp_bit2 + 1));
}

void emit32_merkez_transfer(uint32_t baslangic_ofset) {
    emit8(0x8D); emit8(0xB5); emit32(baslangic_ofset);
    emit8(0xBB); emit32(DILIM_32);
    emit8(0x30); emit8(0xD2);

    size_t dongu = pos2;
    emit8(0x8A); emit8(0x06);
    emit8(0xA8); emit8(0xE0);
    emit8(0x74); size_t jz_normal = pos2; emit8(0);

    emit8(0xC0); emit8(0xE8); emit8(3);
    emit8(0x24); emit8(0x03);

    emit8(0x3C); emit8(1);
    emit8(0x74); size_t je_gecerli = pos2; emit8(0);
    emit8(0x3C); emit8(2);
    emit8(0x75); size_t jne_normal = pos2; emit8(0);

    stage2[je_gecerli] = (uint8_t)(pos2 - (je_gecerli + 1));
    emit8(0x84); emit8(0xD2);
    emit8(0x74); size_t jz_git = pos2; emit8(0);
    emit8(0x38); emit8(0xC2);
    emit8(0x75); size_t jne_arada = pos2; emit8(0);

    stage2[jz_git] = (uint8_t)(pos2 - (jz_git + 1));
    emit8(0x88); emit8(0xC2);
    emit8(0x3C); emit8(1);
    emit8(0x75); size_t jne_sol = pos2; emit8(0);
    emit8(0x01); emit8(0xDE);
    emit8(0xEB); emit8((uint8_t)(dongu - (pos2 + 1)));

    stage2[jne_sol] = (uint8_t)(pos2 - (jne_sol + 1));
    emit8(0x29); emit8(0xDE);
    emit8(0xEB); emit8((uint8_t)(dongu - (pos2 + 1)));

    stage2[jne_arada] = (uint8_t)(pos2 - (jne_arada + 1));
    emit8(0xD1); emit8(0xEB);
    emit8(0x3C); emit8(1);
    emit8(0x75); size_t jne_geri_sol = pos2; emit8(0);
    emit8(0x01); emit8(0xDE);
    emit8(0xEB); size_t jmp_yakalandi = pos2; emit8(0);

    stage2[jne_geri_sol] = (uint8_t)(pos2 - (jne_geri_sol + 1));
    emit8(0x29); emit8(0xDE);

    stage2[jmp_yakalandi] = (uint8_t)(pos2 - (jmp_yakalandi + 1));
    emit8(0xB8); emit32(1);
    emit8(0xEB); size_t jmp_son = pos2; emit8(0);

    stage2[jz_normal]  = (uint8_t)(pos2 - (jz_normal + 1));
    stage2[jne_normal] = (uint8_t)(pos2 - (jne_normal + 1));
    emit8(0x31); emit8(0xC0);

    stage2[jmp_son] = (uint8_t)(pos2 - (jmp_son + 1));
}

void emit32_cumle_paketle(const char *cumle) {
    uint8_t op1_buf[512] = {0};
    size_t op1_bayt = 0;
    int bit_pos = 7;
    uint32_t k_idx = 0;

    for (size_t i = 0; cumle[i]; i++) {
        uint8_t val = 0, len = 2;
        char c = cumle[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c >= 'A' && c <= 'Z') {
            val = (uint8_t)(c - 'A' + 1);
            uint8_t bits = 0, t = val;
            while (t) { bits++; t >>= 1; }
            len = bits + 1;
        }

        uint8_t kilavuz = (uint8_t)((len << 5) | (0b11 << 3));
        emit32_havuz_yaz(2 * DILIM_32 + k_idx++, kilavuz);

        for (int b = len - 1; b >= 0; b--) {
            if ((val >> b) & 1) op1_buf[op1_bayt] |= (uint8_t)(1 << bit_pos);
            if (--bit_pos < 0) { bit_pos = 7; op1_bayt++; }
        }
    }

    emit32_havuz_yaz(2 * DILIM_32 + k_idx, 0x00);
    size_t toplam = (bit_pos == 7) ? op1_bayt : (op1_bayt + 1);
    for (size_t i = 0; i < toplam; i++) {
        emit32_havuz_yaz(DILIM_32 + (uint32_t)i, op1_buf[i]);
    }
}

void emit32_kilavuz_motoru(void) {
    emit8(0x8D); emit8(0xB5); emit32(2 * DILIM_32);
    emit8(0x8D); emit8(0x9D); emit32(DILIM_32);
    emit8(0xB6); emit8(0x80);

    size_t dongu = pos2;
    emit8(0x8A); emit8(0x06);
    emit8(0x88); emit8(0xC2);
    emit8(0xC0); emit8(0xE8); emit8(5);
    emit8(0x84); emit8(0xC0);
    emit8(0x74); size_t jz_bitir = pos2; emit8(0);

    emit8(0xF6); emit8(0xC2); emit8(0x18);
    emit8(0x74); size_t jz_sonraki = pos2; emit8(0);

    emit8(0x0F); emit8(0xB6); emit8(0xC8);
    emit8(0x30); emit8(0xC0);

    size_t bit_topla = pos2;
    emit8(0xD0); emit8(0xE0);
    emit8(0x84); emit8(0x33);
    emit8(0x74); emit8(2);
    emit8(0x0C); emit8(0x01);

    emit8(0xD0); emit8(0xEE);
    emit8(0x75); emit8(3);
    emit8(0xB6); emit8(0x80);
    emit8(0x43);
    emit8(0xE2); emit8((uint8_t)(bit_topla - (pos2 + 1)));

    emit8(0x84); emit8(0xC0);
    emit8(0x75); emit8(4);
    emit8(0xB0); emit8(' ');
    emit8(0xEB); emit8(2);
    emit8(0x04); emit8(64);
    emit8(0xBA); emit32(0x00E5C07B);
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));

    stage2[jz_sonraki] = (uint8_t)(pos2 - (jz_sonraki + 1));
    emit8(0x46);
    emit8(0xEB); emit8((uint8_t)(dongu - (pos2 + 1)));

    stage2[jz_bitir] = (uint8_t)(pos2 - (jz_bitir + 1));
}

void emit32_denklik(void) {
    emit8(0x39); emit8(0xD8);
    emit8(0x0F); emit8(0x94); emit8(0xC0);
    emit8(0x0F); emit8(0xB6); emit8(0xC0);
}

typedef struct {
    size_t dongu_basi;
    size_t cikis_yama;
} FrangeBlok;

FrangeBlok emit32_frange_basla(uint32_t bas, uint32_t son) {
    emit8(0xBE); emit32(bas);
    emit8(0xBA); emit32(son);

    FrangeBlok b;
    b.dongu_basi = pos2;
    emit8(0x39); emit8(0xD6);
    emit8(0x0F); emit8(0x87);
    b.cikis_yama = pos2;
    emit32(0);
    return b;
}

void emit32_frange_bitir(FrangeBlok b) {
    emit8(0x46);
    emit8(0xE9);
    emit32((uint32_t)(b.dongu_basi - (pos2 + 4)));
    emit32_yama32(b.cikis_yama);
}

void emit32_make(uint32_t hedef_ofset, uint32_t kaynak_ofset) {
    emit8(0x8B); emit8(0x85); emit32(kaynak_ofset);
    emit8(0x89); emit8(0x85); emit32(hedef_ofset);
}

void emit32_rotate(uint32_t hedef_ofset) {
    emit8(0xF7); emit8(0x9D); emit32(hedef_ofset);
}

void emit32_end_nokta(void) {
    emit8(0xFA); emit8(0xF4); emit8(0xEB); emit8(0xFE);
}

uint32_t degisken_ofset(const char *ad) {
    while (isspace((unsigned char)*ad)) ad++;
    int idx = (ad[0] >= 'a' && ad[0] <= 'z') ? (ad[0] - 'a') : 0;
    return 490000 + (uint32_t)(idx * 4);
}

void cm_kaynak_derle(const char *dosya_adi) {
    if (!dosya_adi) return;
    FILE *f = fopen(dosya_adi, "r");
    if (!f) return;

    char satir[256];
    FrangeBlok aktif_frange = {0, 0};
    int frange_acik = 0;

    while (fgets(satir, sizeof(satir), f)) {
        char *yorum = strstr(satir, "//");
        if (yorum) *yorum = '\0';
        char *p = satir;
        while (isspace((unsigned char)*p)) p++;
        p[strcspn(p, "\r\n")] = 0;
        if (*p == '\0') continue;

        if (strncmp(p, "end.", 4) == 0) {
            emit32_end_nokta();
            break;
        } else if (strncmp(p, "end", 3) == 0 && frange_acik) {
            emit32_frange_bitir(aktif_frange);
            frange_acik = 0;
        } else if (strncmp(p, "guide.(", 7) == 0 && strstr(p, ">-<")) {
            emit32_kenetle(0, 120000);
        } else if (strncmp(p, "make.(", 6) == 0) {
            char sol[32], sag[32];
            if (sscanf(p + 6, "%31[^=]==%31[^)]", sol, sag) == 2) {
                emit32_make(degisken_ofset(sol), degisken_ofset(sag));
            }
        } else if (strncmp(p, "rotate.(", 8) == 0) {
            char hedef[32];
            if (sscanf(p + 8, "%31[^)]", hedef) == 1) {
                emit32_rotate(degisken_ofset(hedef));
            }
        } else if (strncmp(p, "loop(frange.(", 13) == 0) {
            uint32_t bas = 0, son = 0;
            if (sscanf(p + 13, "%u-%u", &bas, &son) == 2) {
                aktif_frange = emit32_frange_basla(bas, son);
                frange_acik = 1;
            }
        } else if (strncmp(p, "print \"", 7) == 0) {
            char metin[128];
            if (sscanf(p + 7, "%127[^\"]", metin) == 1) {
                emit32_cumle_paketle(metin);
                emit32_kilavuz_motoru();
            }
        }
    }
    fclose(f);
}

// ============================================================================
// VESA GRAFİK, 8x8 FONT, FRAKTAL DÖŞEME ARAYÜZ VE "kullanici"@cm> KABUĞU
// ============================================================================
void emit32_kutu_ciz(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t renk) {
    emit8(0x8B); emit8(0x3D); emit32(0x00009028);
    emit8(0xB8); emit32((y * 1024 + x) * 4);
    emit8(0x01); emit8(0xC7);
    emit8(0xBA); emit32(h);

    size_t satir_dongu = pos2;
    emit8(0x57);
    emit8(0xB9); emit32(w);
    emit8(0xB8); emit32(renk);
    emit8(0xF3); emit8(0xAB);
    emit8(0x5F);
    emit8(0x81); emit8(0xC7); emit32(4096);
    emit8(0x4A);
    emit8(0x75); emit8((uint8_t)(satir_dongu - (pos2 + 1)));
}

void emit32_font_kur(void) {
    emit8(0xE9); size_t jmp_yama = pos2; emit32(0);

    size_t tablo_pos = pos2;
    static const uint8_t font8x8[64][8] = {
        {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 32 ' '
        {0x18,0x18,0x18,0x18,0x18,0x00,0x18,0x00}, // 33 '!'
        {0x6C,0x6C,0x24,0x00,0x00,0x00,0x00,0x00}, // 34 '"'
        {0x6C,0xFE,0x6C,0x6C,0xFE,0x6C,0x00,0x00}, // 35 '#'
        {0x18,0x3E,0x60,0x3C,0x06,0x7C,0x18,0x00}, // 36 '$'
        {0x62,0x64,0x08,0x10,0x26,0x46,0x00,0x00}, // 37 '%'
        {0x38,0x4C,0x38,0x4E,0x64,0x3A,0x00,0x00}, // 38 '&'
        {0x18,0x18,0x30,0x00,0x00,0x00,0x00,0x00}, // 39 '''
        {0x0C,0x18,0x30,0x30,0x30,0x18,0x0C,0x00}, // 40 '('
        {0x30,0x18,0x0C,0x0C,0x0C,0x18,0x30,0x00}, // 41 ')'
        {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // 42 '*'
        {0x00,0x18,0x18,0x7E,0x18,0x18,0x00,0x00}, // 43 '+'
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x30}, // 44 ','
        {0x00,0x00,0x00,0x7E,0x00,0x00,0x00,0x00}, // 45 '-'
        {0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00}, // 46 '.'
        {0x06,0x0C,0x18,0x30,0x60,0xC0,0x80,0x00}, // 47 '/'
        {0x3C,0x66,0x6E,0x76,0x66,0x66,0x3C,0x00}, // 48 '0'
        {0x18,0x38,0x18,0x18,0x18,0x18,0x7E,0x00}, // 49 '1'
        {0x3C,0x66,0x06,0x0C,0x18,0x30,0x7E,0x00}, // 50 '2'
        {0x3C,0x66,0x06,0x1C,0x06,0x66,0x3C,0x00}, // 51 '3'
        {0x0C,0x1C,0x2C,0x4C,0x7E,0x0C,0x0C,0x00}, // 52 '4'
        {0x7E,0x60,0x7C,0x06,0x06,0x66,0x3C,0x00}, // 53 '5'
        {0x1C,0x30,0x60,0x7C,0x66,0x66,0x3C,0x00}, // 54 '6'
        {0x7E,0x66,0x0C,0x18,0x18,0x18,0x18,0x00}, // 55 '7'
        {0x3C,0x66,0x66,0x3C,0x66,0x66,0x3C,0x00}, // 56 '8'
        {0x3C,0x66,0x66,0x3E,0x06,0x0C,0x38,0x00}, // 57 '9'
        {0x00,0x18,0x18,0x00,0x18,0x18,0x00,0x00}, // 58 ':'
        {0x00,0x18,0x18,0x00,0x18,0x18,0x30,0x00}, // 59 ';'
        {0x0C,0x18,0x30,0x60,0x30,0x18,0x0C,0x00}, // 60 '<'
        {0x00,0x00,0x7E,0x00,0x7E,0x00,0x00,0x00}, // 61 '='
        {0x30,0x18,0x0C,0x06,0x0C,0x18,0x30,0x00}, // 62 '>'
        {0x3C,0x66,0x0C,0x18,0x18,0x00,0x18,0x00}, // 63 '?'
        {0x3C,0x66,0x6E,0x6E,0x60,0x62,0x3C,0x00}, // 64 '@'
        {0x18,0x3C,0x66,0x66,0x7E,0x66,0x66,0x00}, // 65 'A'
        {0x7C,0x66,0x66,0x7C,0x66,0x66,0x7C,0x00}, // 66 'B'
        {0x3C,0x66,0x60,0x60,0x60,0x66,0x3C,0x00}, // 67 'C'
        {0x78,0x6C,0x66,0x66,0x66,0x6C,0x78,0x00}, // 68 'D'
        {0x7E,0x60,0x60,0x78,0x60,0x60,0x7E,0x00}, // 69 'E'
        {0x7E,0x60,0x60,0x78,0x60,0x60,0x60,0x00}, // 70 'F'
        {0x3C,0x66,0x60,0x6E,0x66,0x66,0x3E,0x00}, // 71 'G'
        {0x66,0x66,0x66,0x7E,0x66,0x66,0x66,0x00}, // 72 'H'
        {0x3C,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // 73 'I'
        {0x1E,0x0C,0x0C,0x0C,0x0C,0x6C,0x38,0x00}, // 74 'J'
        {0x66,0x6C,0x78,0x70,0x78,0x6C,0x66,0x00}, // 75 'K'
        {0x60,0x60,0x60,0x60,0x60,0x60,0x7E,0x00}, // 76 'L'
        {0x63,0x77,0x7F,0x6B,0x63,0x63,0x63,0x00}, // 77 'M'
        {0x66,0x76,0x7E,0x7E,0x6E,0x66,0x66,0x00}, // 78 'N'
        {0x3C,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 79 'O'
        {0x7C,0x66,0x66,0x7C,0x60,0x60,0x60,0x00}, // 80 'P'
        {0x3C,0x66,0x66,0x66,0x6E,0x3C,0x0E,0x00}, // 81 'Q'
        {0x7C,0x66,0x66,0x7C,0x6C,0x66,0x66,0x00}, // 82 'R'
        {0x3C,0x66,0x60,0x3C,0x06,0x66,0x3C,0x00}, // 83 'S'
        {0x7E,0x18,0x18,0x18,0x18,0x18,0x18,0x00}, // 84 'T'
        {0x66,0x66,0x66,0x66,0x66,0x66,0x3C,0x00}, // 85 'U'
        {0x66,0x66,0x66,0x66,0x66,0x3C,0x18,0x00}, // 86 'V'
        {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // 87 'W'
        {0x66,0x66,0x3C,0x18,0x3C,0x66,0x66,0x00}, // 88 'X'
        {0x66,0x66,0x66,0x3C,0x18,0x18,0x18,0x00}, // 89 'Y'
        {0x7E,0x06,0x0C,0x18,0x30,0x60,0x7E,0x00}, // 90 'Z'
        {0x3C,0x30,0x30,0x30,0x30,0x30,0x3C,0x00}, // 91 '['
        {0xC0,0x60,0x30,0x18,0x0C,0x06,0x02,0x00}, // 92 '\'
        {0x3C,0x0C,0x0C,0x0C,0x0C,0x0C,0x3C,0x00}, // 93 ']'
        {0x18,0x3C,0x66,0x00,0x00,0x00,0x00,0x00}, // 94 '^'
        {0x00,0x00,0x00,0x00,0x00,0x00,0xFF,0x00}  // 95 '_'
    };
    emit_bytes((const uint8_t *)font8x8, sizeof(font8x8));

    font_fn_pos = pos2;
    emit8(0x83); emit8(0xE8); emit8(32);
    emit8(0xC1); emit8(0xE0); emit8(3);
    emit8(0x8D); emit8(0xB0);
    emit32((uint32_t)(0x7E00 + tablo_pos));
    emit8(0xB9); emit32(8);

    size_t satir = pos2;
    emit8(0x8A); emit8(0x1E);
    emit8(0xB7); emit8(0x80);
    emit8(0x57);

    size_t sutun = pos2;
    emit8(0x84); emit8(0xFB);
    emit8(0x74); emit8(2);
    emit8(0x89); emit8(0x17);
    emit8(0x83); emit8(0xC7); emit8(4);
    emit8(0xD0); emit8(0xEF);
    emit8(0x75); emit8((uint8_t)(sutun - (pos2 + 1)));

    emit8(0x5F);
    emit8(0x81); emit8(0xC7); emit32(4096);
    emit8(0x46);
    emit8(0xE2); emit8((uint8_t)(satir - (pos2 + 1)));
    emit8(0xC3);

    emit32_yama32(jmp_yama);
}

void emit32_dinamik_kutu_fn_kur(void) {
    emit8(0xE9); size_t jmp_son = pos2; emit32(0);

    kutu_fn_pos = pos2;
    emit8(0x85); emit8(0xC9);
    emit8(0x74); size_t jz_ret1 = pos2; emit8(0);
    emit8(0x85); emit8(0xD2);
    emit8(0x74); size_t jz_ret2 = pos2; emit8(0);

    emit8(0x8B); emit8(0x3D); emit32(0x00009028);
    emit8(0xC1); emit8(0xE3); emit8(10);
    emit8(0x01); emit8(0xC3);
    emit8(0xC1); emit8(0xE3); emit8(2);
    emit8(0x01); emit8(0xDF);

    size_t satir = pos2;
    emit8(0x57);
    emit8(0x51);
    emit8(0x89); emit8(0xF0);
    emit8(0xF3); emit8(0xAB);
    emit8(0x59);
    emit8(0x5F);
    emit8(0x81); emit8(0xC7); emit32(4096);
    emit8(0x4A);
    emit8(0x75); emit8((uint8_t)(satir - (pos2 + 1)));

    stage2[jz_ret1] = (uint8_t)(pos2 - (jz_ret1 + 1));
    stage2[jz_ret2] = (uint8_t)(pos2 - (jz_ret2 + 1));
    emit8(0xC3);

    emit32_yama32(jmp_son);
}

void emit32_call_kutu_fn(void) {
    emit8(0xE8);
    emit32((uint32_t)((intptr_t)kutu_fn_pos - (intptr_t)(pos2 + 4)));
}

void emit32_ui_tablo_kur(void) {
    const uint32_t geo[5][5] = {
        {   6,  30, 460, 363, 0x00161922 }, // 0: left
        {   6, 399, 460, 363, 0x00161922 }, // 1: op1
        { 472,  30,  80, 732, 0x001B1E28 }, // 2: middle
        { 558,  30, 460, 363, 0x00161922 }, // 3: op2
        { 558, 399, 460, 363, 0x00161922 }  // 4: right
    };
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t off = UI_TABLO + i * 20;
        for (uint32_t j = 0; j < 5; j++) {
            emit8(0xC7); emit8(0x85); emit32(off + j * 4); emit32(geo[i][j]);
        }
    }
    emit8(0xC7); emit8(0x85); emit32(AKTIF_ID); emit32(0);
}

void emit32_term_tablo_kur(void) {
    const uint32_t bas[5][2] = {
        {  16,  40 }, // 0: left
        {  16, 409 }, // 1: op1
        { 482,  40 }, // 2: middle
        { 568,  40 }, // 3: op2
        { 568, 409 }  // 4: right
    };
    for (uint32_t i = 0; i < 5; i++) {
        uint32_t off = TERM_TABLO + i * 16;
        emit8(0xC7); emit8(0x85); emit32(off + 0);  emit32(bas[i][0]);
        emit8(0xC7); emit8(0x85); emit32(off + 4);  emit32(bas[i][1]);
        emit8(0xC7); emit8(0x85); emit32(off + 8);  emit32(bas[i][0]);
        emit8(0xC7); emit8(0x85); emit32(off + 12); emit32(bas[i][1]);
    }
    emit8(0xC7); emit8(0x85); emit32(CMD_LEN); emit32(0);
}

void emit32_bolge_ciz(uint32_t kenar_renk, int icini_de_boya) {
    emit8(0x6B); emit8(0xD8); emit8(20);
    emit8(0x8D); emit8(0xB4); emit8(0x1D); emit32(UI_TABLO);
    emit8(0x83); emit8(0x7E); emit8(0x08); emit8(0);
    emit8(0x0F); emit8(0x84); size_t jz_atla = pos2; emit32(0);

    emit8(0x56);

    if (icini_de_boya) {
        emit8(0x8B); emit8(0x06); emit8(0x83); emit8(0xC0); emit8(2);
        emit8(0x8B); emit8(0x5E); emit8(0x04); emit8(0x83); emit8(0xC3); emit8(2);
        emit8(0x8B); emit8(0x4E); emit8(0x08); emit8(0x83); emit8(0xE9); emit8(4);
        emit8(0x8B); emit8(0x56); emit8(0x0C); emit8(0x83); emit8(0xEA); emit8(4);
        emit8(0x8B); emit8(0x76); emit8(0x10);
        emit32_call_kutu_fn();
        emit8(0x8B); emit8(0x34); emit8(0x24);
    }

    // Üst
    emit8(0x8B); emit8(0x06); emit8(0x8B); emit8(0x5E); emit8(0x04);
    emit8(0x8B); emit8(0x4E); emit8(0x08); emit8(0xBA); emit32(2);
    emit8(0xBE); emit32(kenar_renk); emit32_call_kutu_fn();
    emit8(0x8B); emit8(0x34); emit8(0x24);

    // Alt
    emit8(0x8B); emit8(0x06);
    emit8(0x8B); emit8(0x5E); emit8(0x04); emit8(0x03); emit8(0x5E); emit8(0x0C); emit8(0x83); emit8(0xEB); emit8(2);
    emit8(0x8B); emit8(0x4E); emit8(0x08); emit8(0xBA); emit32(2);
    emit8(0xBE); emit32(kenar_renk); emit32_call_kutu_fn();
    emit8(0x8B); emit8(0x34); emit8(0x24);

    // Sol
    emit8(0x8B); emit8(0x06); emit8(0x8B); emit8(0x5E); emit8(0x04);
    emit8(0xB9); emit32(2); emit8(0x8B); emit8(0x56); emit8(0x0C);
    emit8(0xBE); emit32(kenar_renk); emit32_call_kutu_fn();
    emit8(0x8B); emit8(0x34); emit8(0x24);

    // Sağ
    emit8(0x8B); emit8(0x06); emit8(0x03); emit8(0x46); emit8(0x08); emit8(0x83); emit8(0xE8); emit8(2);
    emit8(0x8B); emit8(0x5E); emit8(0x04);
    emit8(0xB9); emit32(2); emit8(0x8B); emit8(0x56); emit8(0x0C);
    emit8(0xBE); emit32(kenar_renk); emit32_call_kutu_fn();

    emit8(0x5E);
    emit32_yama32(jz_atla);
}

void emit32_wibar_guncelle(void) {
    for (uint32_t i = 0; i < 5; i++) {
        emit8(0xBE); emit32(0x003E4451);

        emit8(0x83); emit8(0xBD); emit32(UI_TABLO + i * 20 + 8); emit8(0);
        emit8(0x75); emit8(5);
        emit8(0xBE); emit32(0x00222630);

        emit8(0x83); emit8(0xBD); emit32(AKTIF_ID); emit8((uint8_t)i);
        emit8(0x75); emit8(5);
        emit8(0xBE); emit32(i == 2 ? 0x00E5C07B : 0x005294E2);

        emit8(0xB8); emit32(8 + i * 28);
        emit8(0xBB); emit32(4);
        emit8(0xB9); emit32(22);
        emit8(0xBA); emit32(16);
        emit32_call_kutu_fn();
    }
}

void emit32_tam_ekran_yenile(void) {
    emit32_kutu_ciz(0, 0, 1024, 768, 0x000F1117);
    emit32_kutu_ciz(0, 0, 1024, 24,  0x00181B24);
    for (uint32_t i = 0; i < 5; i++) {
        emit8(0xB8); emit32(i);
        emit32_bolge_ciz(i == 0 ? 0x005294E2 : 0x003E4451, 1);
    }
    emit32_wibar_guncelle();
}

void emit32_char_fn_kur(void) {
    emit8(0xE9); size_t jmp_son = pos2; emit32(0);

    char_fn_pos = pos2;
    emit8(0x60);

    emit8(0x3C); emit8('a');
    emit8(0x72); emit8(6);
    emit8(0x3C); emit8('z');
    emit8(0x77); emit8(2);
    emit8(0x2C); emit8(32);
    emit8(0x0F); emit8(0xB6); emit8(0xC8);

    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit8(0x89); emit8(0xC3);
    emit8(0xC1); emit8(0xE3); emit8(4);
    emit8(0x8D); emit8(0xBC); emit8(0x1D); emit32(TERM_TABLO);

    emit8(0x6B); emit8(0xC0); emit8(20);
    emit8(0x8D); emit8(0xB4); emit8(0x05); emit32(UI_TABLO);

    emit8(0x57); emit8(0x56); emit8(0x51);
    emit8(0x8B); emit8(0x47); emit8(0x04);
    emit8(0xC1); emit8(0xE0); emit8(10);
    emit8(0x03); emit8(0x07);
    emit8(0xC1); emit8(0xE0); emit8(2);
    emit8(0x03); emit8(0x05); emit32(0x00009028);
    emit8(0x89); emit8(0xC7);
    emit8(0x58);
    emit8(0xE8); emit32((uint32_t)((intptr_t)font_fn_pos - (intptr_t)(pos2 + 4)));
    emit8(0x5E); emit8(0x5F);

    emit8(0x83); emit8(0x07); emit8(9);
    emit8(0x8B); emit8(0x06);
    emit8(0x03); emit8(0x46); emit8(0x08);
    emit8(0x83); emit8(0xE8); emit8(14);
    emit8(0x39); emit8(0x07);
    emit8(0x72); emit8(9);
    emit8(0x8B); emit8(0x47); emit8(0x08);
    emit8(0x89); emit8(0x07);
    emit8(0x83); emit8(0x47); emit8(0x04); emit8(12);

    emit8(0x8B); emit8(0x46); emit8(0x04);
    emit8(0x03); emit8(0x46); emit8(0x0C);
    emit8(0x83); emit8(0xE8); emit8(14);
    emit8(0x39); emit8(0x47); emit8(0x04);
    emit8(0x72); emit8(6);
    emit8(0x8B); emit8(0x47); emit8(0x0C);
    emit8(0x89); emit8(0x47); emit8(0x04);

    emit8(0x61);
    emit8(0xC3);
    emit32_yama32(jmp_son);
}

void emit32_prompt_fn_kur(void) {
    emit8(0xE9); size_t jmp_son = pos2; emit32(0);

    size_t metin_pos = pos2;
    const char *prompt = "\"kullanici\"@cm> ";
    emit_bytes((const uint8_t *)prompt, strlen(prompt) + 1);

    prompt_fn_pos = pos2;
    emit8(0x56); emit8(0x50); emit8(0x52);
    emit8(0xBE); emit32((uint32_t)(0x7E00 + metin_pos));
    emit8(0xBA); emit32(0x0098C379);

    size_t dongu = pos2;
    emit8(0x8A); emit8(0x06);
    emit8(0x84); emit8(0xC0);
    emit8(0x74); emit8(8);
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));
    emit8(0x46);
    emit8(0xEB); emit8((uint8_t)(dongu - (pos2 + 1)));

    emit8(0x5A); emit8(0x58); emit8(0x5E);
    emit8(0xC3);
    emit32_yama32(jmp_son);
}

void emit32_call_prompt(void) {
    emit8(0xE8); emit32((uint32_t)((intptr_t)prompt_fn_pos - (intptr_t)(pos2 + 4)));
}

void emit32_yeni_satir_aktif(void) {
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit8(0xC1); emit8(0xE0); emit8(4);
    emit8(0x8D); emit8(0xBC); emit8(0x05); emit32(TERM_TABLO);
    emit8(0x8B); emit8(0x47); emit8(0x08);
    emit8(0x89); emit8(0x07);
    emit8(0x83); emit8(0x47); emit8(0x04); emit8(12);
}
// PS/2 (i8042) klavye portunu açar, taramayı başlatır ve eski tamponu temizler
void emit32_klavye_baslat(void) {
    uint8_t ps2_init[] = {
        // 1. Tamponda kalan eski baytları boşalt
        0xE4, 0x64,             // in al, 0x64
        0xA8, 0x01,             // test al, 1
        0x74, 0x04,             // jz +4 (enable_port)
        0xE4, 0x60,             // in al, 0x60
        0xEB, 0xF6,             // jmp -10 (flush)

        // 2. Birinci PS/2 klavye portunu aktif et (0xAE -> 0x64)
        0xE4, 0x64,             // in al, 0x64
        0xA8, 0x02,             // test al, 2
        0x75, 0xFA,             // jnz -6
        0xB0, 0xAE,             // mov al, 0xAE
        0xE6, 0x64,             // out 0x64, al

        // 3. Klavye taramasını başlat (0xF4 -> 0x60)
        0xE4, 0x64,             // in al, 0x64
        0xA8, 0x02,             // test al, 2
        0x75, 0xFA,             // jnz -6
        0xB0, 0xF4,             // mov al, 0xF4
        0xE6, 0x60,             // out 0x60, al

        // 4. Klavyeden dönen ACK (0xFA) baytını tüket
        0xB9, 0xA0, 0x86, 0x01, 0x00, // mov ecx, 100000
        0xE4, 0x64,             // in al, 0x64
        0xA8, 0x01,             // test al, 1
        0x75, 0x04,             // jnz +4 (read_ack)
        0xE2, 0xF8,             // loop -8
        0xEB, 0x02,             // jmp +2 (done)
        0xE4, 0x60              // in al, 0x60
    };
    emit_bytes(ps2_init, sizeof(ps2_init));
}
// Başlangıç kök dizinini ve örnek 'ORNEK.CM' + 'PROJELER' dizinini kurar
void emit32_fs_kur(void) {
    emit8(0xC7); emit8(0x85); emit32(AKTIF_DIZIN); emit32(0);
    emit8(0xC7); emit8(0x85); emit32(CME_MOD);     emit32(0);
    emit8(0xC7); emit8(0x85); emit32(FS_SAYAC);    emit32(2);
    emit8(0xC7); emit8(0x85); emit32(FS_VERI_SON); emit32(FS_VERI + 64);

    // Kayıt 0: "PROJELER" (tip = 1: dizin, ust = 0)
    const char *d0 = "PROJELER";
    for (int i = 0; d0[i]; i++) {
        emit8(0xC6); emit8(0x85); emit32(FS_TABLO + i); emit8((uint8_t)d0[i]);
    }
    emit8(0xC7); emit8(0x85); emit32(FS_TABLO + 16); emit32(0); // ust_dizin = 0
    emit8(0xC7); emit8(0x85); emit32(FS_TABLO + 20); emit32(1); // tip = 1 (dizin)

    // Kayıt 1: "ORNEK.CM" (tip = 2: dosya, ust = 0)
    uint32_t k1 = FS_TABLO + 32;
    const char *d1 = "ORNEK.CM";
    for (int i = 0; d1[i]; i++) {
        emit8(0xC6); emit8(0x85); emit32(k1 + i); emit8((uint8_t)d1[i]);
    }
    emit8(0xC7); emit8(0x85); emit32(k1 + 16); emit32(0);       // ust_dizin = 0
    emit8(0xC7); emit8(0x85); emit32(k1 + 20); emit32(2);       // tip = 2 (dosya)
    emit8(0xC7); emit8(0x85); emit32(k1 + 24); emit32(FS_VERI); // veri_ofset
    emit8(0xC7); emit8(0x85); emit32(k1 + 28); emit32(18);      // boyut

    const char *icerik = "PRINT CM-OS HAZIR\n";
    for (int i = 0; icerik[i]; i++) {
        emit8(0xC6); emit8(0x85); emit32(FS_VERI + i); emit8((uint8_t)icerik[i]);
    }
}

// Aktif pencereye sıfır sonlandırmalı metin basar (ESI = adres, EDX = renk)
void emit32_metin_bas_esi(void) {
    size_t dongu = pos2;
    emit8(0x8A); emit8(0x06);                                   // mov al, [esi]
    emit8(0x84); emit8(0xC0);                                   // test al, al
    emit8(0x74); emit8(18);                                     // jz +18 (.son)
    emit8(0x3C); emit8('\n');                                   // cmp al, '\n'
    emit8(0x75); emit8(7);                                      // jne +7 (.karakter)
    emit32_yeni_satir_aktif();
    emit8(0x46);
    emit8(0xEB); emit8((uint8_t)(dongu - (pos2 + 1)));
    // .karakter:
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));
    emit8(0x46);
    emit8(0xEB); emit8((uint8_t)(dongu - (pos2 + 1)));
}

// 'ls' komutu: AKTIF_DIZIN içindeki dizin ve .cm dosyalarını listeler
void emit32_cmd_ls(void) {
    emit8(0x31); emit8(0xC9);                                   // xor ecx, ecx (indeks)
    size_t dongu = pos2;
    emit8(0x3B); emit8(0x8D); emit32(FS_SAYAC);                 // cmp ecx, [FS_SAYAC]
    emit8(0x0F); emit8(0x83); size_t jae_son = pos2; emit32(0);

    emit8(0x89); emit8(0xCE);
    emit8(0xC1); emit8(0xE6); emit8(5);                         // esi = ecx * 32
    emit8(0x8D); emit8(0xB4); emit8(0x35); emit32(FS_TABLO);    // lea esi, [ebp + esi + FS_TABLO]

    // ust_dizin == AKTIF_DIZIN mi?
    emit8(0x8B); emit8(0x46); emit8(16);
    emit8(0x3B); emit8(0x85); emit32(AKTIF_DIZIN);
    emit8(0x0F); emit8(0x85); size_t jne_atla = pos2; emit32(0);

    // Renk: dizin (1) = Mavi, dosya (2) = Açık Yeşil
    emit8(0xBA); emit32(0x0098C379);
    emit8(0x83); emit8(0x7E); emit8(20); emit8(1);
    emit8(0x75); emit8(5);
    emit8(0xBA); emit32(0x005294E2);

    emit8(0x51); emit8(0x56);
    emit32_metin_bas_esi();
    emit8(0xB0); emit8(' ');
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));
    emit8(0x5E); emit8(0x59);

    emit32_yama32(jne_atla);
    emit8(0x41);                                                // inc ecx
    emit8(0xE9); emit32((uint32_t)(dongu - (pos2 + 4)));

    emit32_yama32(jae_son);
    emit32_yeni_satir_aktif();
}

// 'cd <ad>' veya 'cd ..' komutu
void emit32_cmd_cd(void) {
    // "cd .." kontrolü -> köke (0) dön
    emit8(0x66); emit8(0x81); emit8(0xBD); emit32(CMD_BUF + 3); emit8('.'); emit8('.');
    emit8(0x75); emit8(12);
    emit8(0xC7); emit8(0x85); emit32(AKTIF_DIZIN); emit32(0);
    emit8(0xE9); size_t jmp_kok = pos2; emit32(0);

    // Dizin adıyla eşleşen kaydı bul
    emit8(0x31); emit8(0xC9);
    size_t dongu = pos2;
    emit8(0x3B); emit8(0x8D); emit32(FS_SAYAC);
    emit8(0x0F); emit8(0x83); size_t jae_yok = pos2; emit32(0);

    emit8(0x89); emit8(0xCE);
    emit8(0xC1); emit8(0xE6); emit8(5);
    emit8(0x8D); emit8(0xB4); emit8(0x35); emit32(FS_TABLO);

    emit8(0x83); emit8(0x7E); emit8(20); emit8(1);              // tip == 1 (dizin) mi?
    emit8(0x75); emit8(22);

    // İlk 4 harfi büyük/küçük duyarsız karşılaştır
    emit8(0x8B); emit8(0x85); emit32(CMD_BUF + 3);
    emit8(0x25); emit32(0xDFDFDFDF);
    emit8(0x3B); emit8(0x06);
    emit8(0x75); emit8(9);
    emit8(0x89); emit8(0x8D); emit32(AKTIF_DIZIN);              // AKTIF_DIZIN = ecx
    emit8(0xE9); size_t jmp_buldu = pos2; emit32(0);

    emit8(0x41);
    emit8(0xE9); emit32((uint32_t)(dongu - (pos2 + 4)));

    emit32_yama32(jae_yok);
    emit32_yama32(jmp_kok);
    emit32_yama32(jmp_buldu);
}

// 'cme <dosya.cm>' editörünü açar (var olanı yükler veya yeni dosya oluşturur)
void emit32_cmd_cme_ac(void) {
    // Ekranı temizle ve üst başlık çubuğunu çiz
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit32_bolge_ciz(0x00E5C07B, 1);                            // Altın editör çerçevesi
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit8(0xC1); emit8(0xE0); emit8(4);
    emit8(0x8D); emit8(0xBC); emit8(0x05); emit32(TERM_TABLO);
    emit8(0x8B); emit8(0x47); emit8(0x08); emit8(0x89); emit8(0x07);
    emit8(0x8B); emit8(0x47); emit8(0x0C); emit8(0x89); emit8(0x47); emit8(0x04);

    // Yeni kayıt oluştur: FS_TABLO[FS_SAYAC]
    emit8(0x8B); emit8(0x8D); emit32(FS_SAYAC);
    emit8(0x89); emit8(0xCE);
    emit8(0xC1); emit8(0xE6); emit8(5);
    emit8(0x8D); emit8(0xBD); emit8(0x35); emit32(FS_TABLO);    // EDI = yeni kayıt
    emit8(0x89); emit8(0xBD); emit32(CME_KAYIT);

    // Dosya adını (CMD_BUF + 4) büyük harfe çevirip kayda yaz
    emit8(0x31); emit8(0xC9);
    size_t ad_kopya = pos2;
    emit8(0x8A); emit8(0x84); emit8(0x0D); emit32(CMD_BUF + 4);
    emit8(0x84); emit8(0xC0);
    emit8(0x74); emit8(15);
    emit8(0x3C); emit8('a'); emit8(0x72); emit8(6);
    emit8(0x3C); emit8('z'); emit8(0x77); emit8(2);
    emit8(0x2C); emit8(32);
    emit8(0x88); emit8(0x04); emit8(0x0F);
    emit8(0x41);
    emit8(0x83); emit8(0xF9); emit8(15);
    emit8(0x72); emit8((uint8_t)(ad_kopya - (pos2 + 1)));
    emit8(0xC6); emit8(0x04); emit8(0x0F); emit8(0);

    emit8(0x8B); emit8(0x85); emit32(AKTIF_DIZIN);
    emit8(0x89); emit8(0x47); emit8(16);                        // ust_dizin
    emit8(0xC7); emit8(0x47); emit8(20); emit32(2);             // tip = 2 (dosya)
    emit8(0x8B); emit8(0x85); emit32(FS_VERI_SON);
    emit8(0x89); emit8(0x47); emit8(24);                        // veri_ofset
    emit8(0xC7); emit8(0x47); emit8(28); emit32(0);             // boyut = 0
    emit8(0xFF); emit8(0x85); emit32(FS_SAYAC);
    emit8(0xC7); emit8(0x85); emit32(CME_MOD); emit32(1);       // CME editör modunu aç
}

void emit32_komut_satiri_dongusu(void) {
    emit32_call_prompt();

    size_t ana_dongu = pos2;
    emit32_klavye_oku();

    // --- 0. CME EDİTÖR MODU KONTROLÜ (CME_MOD != 0 ise editöre gir) ---
    emit8(0x83); emit8(0xBD); emit32(CME_MOD); emit8(0);
    emit8(0x0F); emit8(0x84); size_t jz_kabuk = pos2; emit32(0);

    // Esc (0x1B) -> Dosyayı mühürle ve kabuğa dön
    emit8(0x3C); emit8(0x1B);
    emit8(0x0F); emit8(0x85); size_t jne_cme_enter = pos2; emit32(0);

    emit8(0x8B); emit8(0xB5); emit32(CME_KAYIT);
    emit8(0x8B); emit8(0x5E); emit8(24);                        // veri_ofset
    emit8(0x03); emit8(0x5E); emit8(28);                        // + boyut
    emit8(0xC6); emit8(0x04); emit8(0x2B); emit8(0);            // mov byte [ebx + ebp], 0
    emit8(0x81); emit8(0x85); emit32(FS_VERI_SON); emit32(4096);
    emit8(0xC7); emit8(0x85); emit32(CME_MOD); emit32(0);
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit32_bolge_ciz(0x005294E2, 0);
    emit32_yeni_satir_aktif();
    emit32_call_prompt();
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    // Enter (0x0D) -> Dosyaya '\n' ekle ve alt satıra geç
    emit32_yama32(jne_cme_enter);
    emit8(0x3C); emit8(0x0D);
    emit8(0x0F); emit8(0x85); size_t jne_cme_karakter = pos2; emit32(0);

    emit8(0x8B); emit8(0xB5); emit32(CME_KAYIT);
    emit8(0x8B); emit8(0x5E); emit8(24);
    emit8(0x03); emit8(0x5E); emit8(28);
    emit8(0xC6); emit8(0x04); emit8(0x2B); emit8('\n');         // mov byte [ebx + ebp], '\n'
    emit8(0xFF); emit8(0x46); emit8(28);
    emit32_yeni_satir_aktif();
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    // Normal karakter -> Dosya tamponuna yaz ve ekrana bas
    emit32_yama32(jne_cme_karakter);
    emit8(0x3C); emit8(32);
    emit8(0x0F); emit8(0x82); size_t jb_cme_son = pos2; emit32(0);

    emit8(0x8B); emit8(0xB5); emit32(CME_KAYIT);
    emit8(0x8B); emit8(0x5E); emit8(24);
    emit8(0x03); emit8(0x5E); emit8(28);
    emit8(0x88); emit8(0x04); emit8(0x2B);                      // mov [ebx + ebp], al
    emit8(0xFF); emit8(0x46); emit8(28);
    emit8(0xBA); emit32(0x00ECEFF4);
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));

    emit32_yama32(jb_cme_son);
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    // ========================================================================
    // KABUK MODU (CME_MOD == 0)
    // ========================================================================
    emit32_yama32(jz_kabuk);

    // --- 1. TAB (0x09): ODAK DEĞİŞTİR ---
    emit8(0x3C); emit8(0x09);
    emit8(0x0F); emit8(0x85); size_t jne_bs = pos2; emit32(0);

    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit32_bolge_ciz(0x003E4451, 0);

    size_t sonraki_bolge = pos2;
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit8(0x40);
    emit8(0x83); emit8(0xF8); emit8(5);
    emit8(0x72); emit8(2);
    emit8(0x31); emit8(0xC0);
    emit8(0x89); emit8(0x85); emit32(AKTIF_ID);
    emit8(0x6B); emit8(0xD8); emit8(20);
    emit8(0x83); emit8(0xBC); emit8(0x1D); emit32(UI_TABLO + 8); emit8(0);
    emit8(0x74); emit8((uint8_t)(sonraki_bolge - (pos2 + 1)));

    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit32_bolge_ciz(0x005294E2, 0);
    emit32_wibar_guncelle();
    emit8(0xC7); emit8(0x85); emit32(CMD_LEN); emit32(0);
    emit32_call_prompt();
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    // --- 2. BACKSPACE (0x08): SON HARFİ SİL ---
    emit32_yama32(jne_bs);
    emit8(0x3C); emit8(0x08);
    emit8(0x0F); emit8(0x85); size_t jne_enter = pos2; emit32(0);

    emit8(0x83); emit8(0xBD); emit32(CMD_LEN); emit8(0);
    emit8(0x0F); emit8(0x84); size_t jz_bs_son = pos2; emit32(0);

    emit8(0xFF); emit8(0x8D); emit32(CMD_LEN);
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit8(0x89); emit8(0xC3);
    emit8(0x6B); emit8(0xDB); emit8(20);
    emit8(0x8B); emit8(0xB4); emit8(0x1D); emit32(UI_TABLO + 16);

    emit8(0xC1); emit8(0xE0); emit8(4);
    emit8(0x8D); emit8(0xBC); emit8(0x05); emit32(TERM_TABLO);
    emit8(0x83); emit8(0x2F); emit8(9);

    emit8(0x8B); emit8(0x07);
    emit8(0x8B); emit8(0x5F); emit8(0x04);
    emit8(0xB9); emit32(9);
    emit8(0xBA); emit32(10);
    emit32_call_kutu_fn();

    emit32_yama32(jz_bs_son);
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    // --- 3. ENTER (0x0D): KOMUTU ÇALIŞTIR ---
    emit32_yama32(jne_enter);
    emit8(0x3C); emit8(0x0D);
    emit8(0x0F); emit8(0x85); size_t jne_yaz = pos2; emit32(0);

    emit8(0x8B); emit8(0x9D); emit32(CMD_LEN);
    emit8(0xC6); emit8(0x84); emit8(0x1D); emit32(CMD_BUF); emit8(0);
    emit32_yeni_satir_aktif();

    // Komut A: "clear"
    emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('c'); emit8('l'); emit8('e'); emit8('a');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_reset = pos2; emit32(0);
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit32_bolge_ciz(0x005294E2, 1);
    emit8(0x8B); emit8(0x85); emit32(AKTIF_ID);
    emit8(0xC1); emit8(0xE0); emit8(4);
    emit8(0x8D); emit8(0xBC); emit8(0x05); emit32(TERM_TABLO);
    emit8(0x8B); emit8(0x47); emit8(0x08); emit8(0x89); emit8(0x07);
    emit8(0x8B); emit8(0x47); emit8(0x0C); emit8(0x89); emit8(0x47); emit8(0x04);
    emit8(0xE9); size_t jmp_cmd_bitti1 = pos2; emit32(0);

    // Komut B: "reset"
    emit32_yama32(jne_cmd_reset);
    emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('r'); emit8('e'); emit8('s'); emit8('e');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_merge = pos2; emit32(0);
    emit32_tablo_kur();
    emit32_ui_tablo_kur();
    emit32_term_tablo_kur();
    emit32_tam_ekran_yenile();
    emit8(0xE9); size_t jmp_cmd_bitti2 = pos2; emit32(0);

    // Komut C: "merge"
    emit32_yama32(jne_cmd_merge);
    emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('m'); emit8('e'); emit8('r'); emit8('g');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_print = pos2; emit32(0);
    emit32_kenetle(0, 120000);
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 12); emit32(732);
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 28); emit32(0);
    emit8(0xC7); emit8(0x85); emit32(AKTIF_ID); emit32(0);
    emit8(0xB8); emit32(0);
    emit32_bolge_ciz(0x005294E2, 1);
    emit32_wibar_guncelle();
    emit8(0x8D); emit8(0xBD); emit32(TERM_TABLO);
    emit8(0x8B); emit8(0x47); emit8(0x08); emit8(0x89); emit8(0x07);
    emit8(0x8B); emit8(0x47); emit8(0x0C); emit8(0x89); emit8(0x47); emit8(0x04);
    emit8(0xE9); size_t jmp_cmd_bitti3 = pos2; emit32(0);
    
    // Komut C2: "topmerge" -> Üst iki terminali (left [0] ve op2 [3]) birleştirir
    emit32_yama32(jne_cmd_print);
    emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('t'); emit8('o'); emit8('p'); emit8('m');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_not_topmerge = pos2; emit32(0);

    // 5'li havuzda kenetlemeleri uygula (0 -> 1 -> 2 -> 3)
    emit32_kenetle(0, 120000);
    emit32_kenetle(1, 120000);
    emit32_kenetle(2, 120000);

    // UI_TABLO güncelle:
    // 0 (left): x=6, y=30, w=1012, h=363 (üst tam genişlik)
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 0 * 20 + 0);  emit32(6);
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 0 * 20 + 4);  emit32(30);
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 0 * 20 + 8);  emit32(1012);
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 0 * 20 + 12); emit32(363);

    // 2 (middle) ve 3 (op2) üst sırada yutulsun (w = 0)
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 2 * 20 + 8);  emit32(0);
    emit8(0xC7); emit8(0x85); emit32(UI_TABLO + 3 * 20 + 8);  emit32(0);

    // Odağı 0 (left) yap, çiz ve Wibar'ı güncelle
    emit8(0xC7); emit8(0x85); emit32(AKTIF_ID); emit32(0);
    emit8(0xB8); emit32(0);
    emit32_bolge_ciz(0x005294E2, 1);
    emit32_wibar_guncelle();

    // Aktif terminal imlecini başa al
    emit8(0x8D); emit8(0xBD); emit32(TERM_TABLO);
    emit8(0x8B); emit8(0x47); emit8(0x08); emit8(0x89); emit8(0x07);
    emit8(0x8B); emit8(0x47); emit8(0x0C); emit8(0x89); emit8(0x47); emit8(0x04);
    emit8(0xE9); size_t jmp_cmd_bitti3b = pos2; emit32(0);

    // Komut D: "print ..."
    emit32_yama32(jne_cmd_not_topmerge);
    emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('p'); emit8('r'); emit8('i'); emit8('n');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_ls = pos2; emit32(0);
    emit8(0x8D); emit8(0xB5); emit32(CMD_BUF + 6);
    emit8(0xBA); emit32(0x00E5C07B);
    size_t prt_dongu = pos2;
    emit8(0x8A); emit8(0x06);
    emit8(0x84); emit8(0xC0);
    emit8(0x74); emit8(8);
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));
    emit8(0x46);
    emit8(0xEB); emit8((uint8_t)(prt_dongu - (pos2 + 1)));
    emit32_yeni_satir_aktif();
    emit8(0xE9); size_t jmp_cmd_bitti4 = pos2; emit32(0);

    // Komut E: "ls"
    emit32_yama32(jne_cmd_ls);
    emit8(0x66); emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('l'); emit8('s');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_cd = pos2; emit32(0);
    emit32_cmd_ls();
    emit8(0xE9); size_t jmp_cmd_bitti5 = pos2; emit32(0);

    // Komut F: "cd ..."
    emit32_yama32(jne_cmd_cd);
    emit8(0x66); emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('c'); emit8('d');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_cme = pos2; emit32(0);
    emit32_cmd_cd();
    emit8(0xE9); size_t jmp_cmd_bitti6 = pos2; emit32(0);

    // Komut G: "cme ..."
    emit32_yama32(jne_cmd_cme);
    emit8(0x81); emit8(0xBD); emit32(CMD_BUF); emit8('c'); emit8('m'); emit8('e'); emit8(' ');
    emit8(0x0F); emit8(0x85); size_t jne_cmd_son = pos2; emit32(0);
    emit32_cmd_cme_ac();
    emit8(0xC7); emit8(0x85); emit32(CMD_LEN); emit32(0);
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    emit32_yama32(jne_cmd_son);
    emit32_yama32(jmp_cmd_bitti1);
    emit32_yama32(jmp_cmd_bitti2);
    emit32_yama32(jmp_cmd_bitti3);
    emit32_yama32(jmp_cmd_bitti3b);
    emit32_yama32(jmp_cmd_bitti4);
    emit32_yama32(jmp_cmd_bitti5);
    emit32_yama32(jmp_cmd_bitti6);

    emit8(0xC7); emit8(0x85); emit32(CMD_LEN); emit32(0);
    emit32_call_prompt();
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));

    // --- 4. NORMAL KARAKTER ---
    emit32_yama32(jne_yaz);
    emit8(0x3C); emit8(32);
    emit8(0x0F); emit8(0x82); size_t jb_son = pos2; emit32(0);

    emit8(0x8B); emit8(0x9D); emit32(CMD_LEN);
    emit8(0x83); emit8(0xFB); emit8(120);
    emit8(0x0F); emit8(0x83); size_t jae_son = pos2; emit32(0);

    emit8(0x88); emit8(0x84); emit8(0x1D); emit32(CMD_BUF);
    emit8(0xFF); emit8(0x85); emit32(CMD_LEN);
    emit8(0xBA); emit32(0x00ECEFF4);
    emit8(0xE8); emit32((uint32_t)((intptr_t)char_fn_pos - (intptr_t)(pos2 + 4)));

    emit32_yama32(jb_son);
    emit32_yama32(jae_son);
    emit8(0xE9); emit32((uint32_t)(ana_dongu - (pos2 + 4)));
}
// ============================================================================
// STAGE 2 ANA OLUŞTURUCU
// ============================================================================
void stage2_olustur(const char *cm_dosya) {
    memset(stage2, 0, sizeof(stage2));
    pos2 = 0;

    emit_vesa_1024x768();
    emit_32bit_gecis();
    emit32_havuz_temizle();
    emit32_tablo_kur();
    emit32_fs_kur();
    emit32_font_kur();
    emit32_dinamik_kutu_fn_kur();
    emit32_ui_tablo_kur();
    emit32_term_tablo_kur();
    emit32_char_fn_kur();
    emit32_prompt_fn_kur();
    emit32_tam_ekran_yenile();

    if (cm_dosya) cm_kaynak_derle(cm_dosya);

    emit32_komut_satiri_dongusu();
}

int main(int argc, char *argv[]) {
    const char *cikti    = (argc >= 2) ? argv[1] : "cm_os.bin";
    const char *cm_dosya = (argc >= 3) ? argv[2] : NULL;

    stage1_olustur();
    stage2_olustur(cm_dosya);

    FILE *f = fopen(cikti, "wb");
    if (!f) return 1;
    fwrite(stage1, 1, 512, f);
    fwrite(stage2, 1, STAGE2_BOYUT, f);
    fclose(f);

    printf("[CM-OS]: '%s' uretildi (Stage1: %zu/510 B | Stage2: %zu/%d B).\n",
           cikti, pos1, pos2, STAGE2_BOYUT);
    return 0;
}
