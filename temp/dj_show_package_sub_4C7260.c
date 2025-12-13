BOOL __thiscall sub_4C7260(CCommonDialog *this, HDC hdc)
{
    BOOL result;     // eax
    int v4;          // ebp
    _DWORD *v5;      // edi
    int v6;          // eax
    const CHAR *v7;  // ebx
    int v8;          // eax
    int v9;          // eax
    const CHAR *v10; // eax
    int v11;         // eax
    int v12;         // eax
    int v13;         // [esp-Ch] [ebp-34h]
    const CHAR *v14; // [esp-8h] [ebp-30h]
    LPCSTR lpString; // [esp+Ch] [ebp-1Ch] BYREF
    _DWORD *v16;     // [esp+10h] [ebp-18h]
    int v17;         // [esp+14h] [ebp-14h]
    int v18;         // [esp+18h] [ebp-10h]
    int v19;         // [esp+24h] [ebp-4h]

    result = CCommonDialog::OnHelpInfo(this, (struct tagHELPINFO *)hdc);
    if (result)
    {
        v4 = 0;
        sub_4F4BE0(hdc, *((_DWORD *)this + 5), *((_DWORD *)this + 6), *((_DWORD *)this + 22), *((_DWORD *)this + 23), 0);
        sub_4B32A0(hdc);
        v5 = (_DWORD *)sub_4892D0(&dword_5585C0);
        v16 = v5;
        if (v5)
        {
            v17 = 0;
            v18 = 0;
            v19 = 0;
            do
            {
                v6 = sub_47F320(v5, v4);
                v7 = (const CHAR *)v6;
                if (v6 && sub_4D0A60(*(_DWORD *)(v6 + 24)))
                {
                    v17 = *((_DWORD *)this + 5) + sub_4D0230((char *)5, 1) + 24 * (v4 % 10);
                    v18 = *((_DWORD *)this + 6) + sub_4D0230((char *)6, 1) + 24 * (v4 / 10);
                    sub_4C7100((int)hdc, v17, v18, v7, 0, -1);
                    v5 = v16;
                }
                ++v4;
            } while (v4 < 50);
            v8 = *((_DWORD *)this + 42);
            if (v8)
            {
                v9 = sub_4D0210(*(_DWORD **)(v8 + 80), 16);
                if (v9 != 0xFFFF)
                {
                    v10 = (const CHAR *)sub_4D0A70(v9, 1);
                    sub_4B1DF0(hdc, v10, -10000, -10000, 1);
                }
            }
            lpString = (LPCSTR)off_546988;
            v11 = v5[8];
            LOBYTE(v19) = 1;
            sub_5196AE((CString *)&lpString, (unsigned __int8 *)aD, v11);
            v14 = lpString;
            v13 = *((_DWORD *)this + 6) + sub_4D0230((char *)6, 2);
            v12 = sub_4D0230((char *)5, 2);
            sub_4B25A0((COLORREF *)this, (int)hdc, *((_DWORD *)this + 5) + v12, v13, v14, 0);
            LOBYTE(v19) = 0;
            sub_51D6E4(&lpString);
            result = 1;
        }
        else
        {
            result = 0;
        }
    }
    return result;
}

BOOL __thiscall CCommonDialog::OnHelpInfo(CCommonDialog *this, struct tagHELPINFO *a2)
{
    return sub_4B1DB0(this) != 0;
}

int __thiscall sub_4B1DB0(_DWORD *this)
{
    int result; // eax

    if (this[26])
        return 1;
    if (!this[25])
        return 0;
    result = 1;
    if (!this[46])
        this[46] = 1;
    return result;
}

int __thiscall sub_4F4BE0(void *this, int a2, int a3, int a4, int a5, int a6, int a7)
{
    sub_4F4D60(this);
    return sub_4F4560(a2, a3, a4, a5, a6, a7);
}

int __thiscall sub_4F4D60(_DWORD *this)
{
    return *this + 22 * this[1];
}

int __thiscall sub_4F4560(__int16 *this, int a2, int a3, int a4, int a5, int a6, int a7)
{
    int v8;  // edi
    int v9;  // ebx
    int v10; // eax

    v8 = *(_DWORD *)(((int (*)(void))sub_4F5CF0)() + 16);
    v9 = *(_DWORD *)(sub_4F5CF0(this) + 12);
    v10 = sub_4F5CF0(this);
    return (*(int(__thiscall **)(int, int, int, int, int, int, _DWORD, _DWORD, int, int, int))(*(_DWORD *)v10 + 12))(
        v10,
        a2,
        a3 + this[7] - this[9],
        a4 + this[8] - this[10],
        v9,
        v8,
        0,
        0,
        a5,
        a6,
        a7);
}

void __thiscall sub_4B32A0(_DWORD *this, int a2)
{
    int v3; // ecx
    int v4; // eax

    v3 = this[39];
    for (this[40] = v3; v3; this[40] = v3)
    {
        (*(void(__thiscall **)(int, int))(*(_DWORD *)v3 + 8))(v3, a2);
        v4 = this[40];
        if (!v4)
            break;
        v3 = *(_DWORD *)(v4 + 8);
    }
}

unsigned int sub_4892D0()
{
    int v0;              // eax
    unsigned int result; // eax

    if (dword_5585C4 && (v0 = sub_477790(*(_DWORD *)(dword_5585C4 + 48))) != 0)
        result = sub_4892B0(*(_DWORD *)(v0 + 24));
    else
        result = 0;
    return result;
}

int __thiscall sub_477790(int *this)
{
    return sub_420D50(this[3]);
}

int __cdecl sub_420D50(int a1)
{
    int v1;     // eax
    int result; // eax
    int v3;     // ecx

    if (!dword_558E50 || !a1 || (v1 = dword_558E50 + 6 * (a1 & 0x7FFF), ((a1 >> 15) & 3) != *(_BYTE *)(v1 + 1)) || (result = *(_DWORD *)(v1 + 2)) == 0 || (v3 = *(_DWORD *)(result + 103), v3 < 30) || v3 > 100)
    {
        result = 0;
    }
    return result;
}

unsigned int __stdcall sub_4892B0(int a1)
{
    return a1 != -1 ? (unsigned int)dword_558AC4 : 0;
}

int __thiscall sub_47F320(_DWORD *this, int a2)
{
    int result; // eax

    if (this[a2 + 41] == -1)
        result = 0;
    else
        result = sub_4D2570(this + 35, this[a2 + 41]);
    return result;
}

int __thiscall sub_4D2570(_DWORD *this, int a2)
{
    int result; // eax

    if (a2 < 0 || a2 > *this - 1)
        result = 0;
    else
        result = *(_DWORD *)(this[5] + 4 * a2);
    return result;
}

// sub_4D0230 这个函数好像是做展示物品的字符串拼接的。如果没有特殊必要，这里可以不用深入解析
int __thiscall sub_4D0230(_DWORD *this, char *String, int a3)
{
    int v3;      // esi
    int v4;      // eax
    int v5;      // esi
    char v7[28]; // [esp+4h] [ebp-28h] BYREF
    int v8;      // [esp+28h] [ebp-4h]

    v3 = sub_4D00B0(this, (int)String);
    if (v3)
    {
        sub_51986C(10);
        v8 = 0;
        sub_4CF730(v3, 42, (CStringList *)v7);
        String = (char *)off_546988;
        LOBYTE(v8) = 1;
        v4 = sub_519ABF(a3);
        if (v4)
        {
            sub_51D7D1((CString *)&String, v4 + 8);
            v5 = atoi(String);
            LOBYTE(v8) = 0;
            sub_51D6E4(&String);
            v8 = -1;
            sub_5198E0(v7);
            return v5;
        }
        LOBYTE(v8) = 0;
        sub_51D6E4(&String);
        v8 = -1;
        sub_5198E0(v7);
    }
    return 0xFFFF;
}

int __thiscall sub_4D00B0(_DWORD *this, int a2)
{
    int v2; // ecx

    if (a2 >= this[1] || a2 < 0)
        return 0;
    v2 = *(_DWORD *)(this[3] + 4 * a2);
    return *(_DWORD *)(v2 - 8) != 0 ? v2 : 0;
}

_DWORD *__thiscall sub_51986C(_DWORD *this, int a2)
{
    _DWORD *result; // eax

    result = this;
    this[3] = 0;
    this[4] = 0;
    this[2] = 0;
    this[1] = 0;
    this[5] = 0;
    *this = &CStringList::`vftable`;
    this[6] = a2;
    return result;
}

int __cdecl sub_4CF730(int a1, int a2, CStringList *a3)
{
    int result;     // eax
    int v4;         // ebp
    int i;          // esi
    CHAR v6[10240]; // [esp+10h] [ebp-2800h] BYREF

    CStringList::RemoveAll(a3);
    result = sub_4D2D40(a1, a2);
    v4 = result;
    for (i = 0; i < v4; ++i)
    {
        if (sub_4D2E40(a1, v6, 10240, i, a2))
            result = sub_51999D(a3, v6);
        else
            result = sub_51999D(a3, &Str2);
    }
    return result;
}