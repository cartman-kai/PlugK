int __thiscall sub_47FEE0(_DWORD *this, int a2, int a3)
{
    int v5; // eax
    int v6; // eax

    if (a2 == -1)
        return 0;
    v5 = sub_4D2570(this + 35, a2);
    if (v5)
    {
        if (a3 == -1)
            *(_DWORD *)(v5 + 28) = 0;
        else
            *(_DWORD *)(v5 + 28) -= a3;
        if (*(int *)(v5 + 28) <= 0)
        {
            this[34] = v5;
            sub_4CF550(this + 26);
            v6 = sub_47F2B0(a2);
            if (v6 != -1)
                sub_47F350(v6, -1);
            if (this[177] == a2)
                sub_47F450(-1);
        }
        sub_47EA20(this);
    }
    return 1;
}

int __thiscall sub_4CF550(_DWORD *this)
{
    int v1; // esi
    int v2; // edi

    v1 = this[8];
    if (!v1)
        return 0;
    v2 = *(_DWORD *)(v1 + 8);
    sub_4CF3D0(v1);
    (**(void(__thiscall ***)(int, int))v1)(v1, 1);
    return v2;
}

int __thiscall sub_4CF3D0(_DWORD *this)
{
    int v1;     // eax
    int v2;     // eax
    int v3;     // eax
    int v4;     // eax
    int v5;     // eax
    int result; // eax

    v1 = this[1];
    if (v1)
    {
        --*(_DWORD *)(v1 + 20);
        v2 = this[1];
        if (*(_DWORD *)(v2 + 20))
        {
            if (*(_DWORD **)(v2 + 28) == this)
            {
                *(_DWORD *)(v2 + 28) = this[2];
                *(_DWORD *)(this[1] + 32) = this[2];
            }
            v3 = this[1];
            if (*(_DWORD **)(v3 + 24) == this)
            {
                *(_DWORD *)(v3 + 24) = this[3];
                *(_DWORD *)(this[1] + 32) = this[3];
            }
            v4 = this[1];
            if (*(_DWORD **)(v4 + 32) == this)
                *(_DWORD *)(v4 + 32) = this[2];
        }
        else
        {
            *(_DWORD *)(v2 + 28) = 0;
            *(_DWORD *)(this[1] + 24) = 0;
            *(_DWORD *)(this[1] + 32) = 0;
        }
    }
    v5 = this[2];
    if (v5)
        *(_DWORD *)(v5 + 12) = this[3];
    result = this[3];
    if (result)
        *(_DWORD *)(result + 8) = this[2];
    this[2] = 0;
    this[3] = 0;
    this[1] = 0;
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
