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