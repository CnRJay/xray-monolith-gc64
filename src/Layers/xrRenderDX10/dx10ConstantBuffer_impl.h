#ifndef	dx10ConstantBuffer_impl_included
#define	dx10ConstantBuffer_impl_included
#pragma once

extern int ps_r4_cbuffer_skip_unchanged;

IC void* dx10ConstantBuffer::RawData(u16 offset)
{
	VERIFY(offset < (int)m_uiBufferSize);
	return ((BYTE*)m_pBufferData) + offset;
}

IC Fvector4* dx10ConstantBuffer::Access(u16 offset)
{
	m_bChanged = true;
	return (Fvector4*)RawData(offset);
}

IC void dx10ConstantBuffer::WriteChecked(u32 offset, const void* src, u32 bytes)
{
	VERIFY(offset + bytes <= m_uiBufferSize);
	BYTE* dst = ((BYTE*)m_pBufferData) + offset;
	if (m_bChanged || !ps_r4_cbuffer_skip_unchanged)
	{
		CopyMemory(dst, src, bytes);
		m_bChanged = true;
	}
	else if (memcmp(dst, src, bytes) != 0)
	{
		CopyMemory(dst, src, bytes);
		m_bChanged = true;
	}
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fmatrix& A)
{
	VERIFY(RC_float == C->type);

	Fvector4 rows[4];
	u32 rowCount = 0;
	switch (L.cls)
	{
	case RC_2x4:
		rows[0].set(A._11, A._21, A._31, A._41);
		rows[1].set(A._12, A._22, A._32, A._42);
		rowCount = 2;
		break;
	case RC_3x4:
		rows[0].set(A._11, A._21, A._31, A._41);
		rows[1].set(A._12, A._22, A._32, A._42);
		rows[2].set(A._13, A._23, A._33, A._43);
		rowCount = 3;
		break;
	case RC_4x4:
		rows[0].set(A._11, A._21, A._31, A._41);
		rows[1].set(A._12, A._22, A._32, A._42);
		rows[2].set(A._13, A._23, A._33, A._43);
		rows[3].set(A._14, A._24, A._34, A._44);
		rowCount = 4;
		break;
	default:
#ifdef DEBUG
		Debug.fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", *C->name);
#else
		NODEFAULT;
#endif
	}
	WriteChecked((u32)L.index, rows, rowCount * lineSize);
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fvector4& A)
{
	VERIFY(RC_float == C->type);
	VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls || RC_1x1 == L.cls);

	u32 count = 4;
	switch (L.cls)
	{
	case RC_1x1: count = 1; break;
	case RC_1x2: count = 2; break;
	case RC_1x3: count = 3; break;
	case RC_1x4: count = 4; break;
	default: break;
	}

	WriteChecked((u32)L.index, &A[0], count * sizeof(float));
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, float A)
{
	VERIFY(RC_float == C->type);
	VERIFY(RC_1x1 == L.cls);
	VERIFY(u32((u32)L.index + sizeof(float)) <= m_uiBufferSize);
	float* it = (float*)RawData(L.index);
	if (m_bChanged || !ps_r4_cbuffer_skip_unchanged || memcmp(it, &A, sizeof(float)) != 0)
	{
		*it = A;
		m_bChanged = true;
	}
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, int A)
{
	VERIFY(RC_int == C->type);
	VERIFY(RC_1x1 == L.cls);
	VERIFY(u32((u32)L.index + sizeof(int)) <= m_uiBufferSize);
	int* it = (int*)RawData(L.index);
	if (m_bChanged || !ps_r4_cbuffer_skip_unchanged || *it != A)
	{
		*it = A;
		m_bChanged = true;
	}
}

IC void dx10ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fmatrix& A)
{
	VERIFY(RC_float == C->type);
	Fvector4 rows[4];
	u32 rowCount = 0;
	switch (L.cls)
	{
	case RC_2x4:
		rows[0].set(A._11, A._21, A._31, A._41);
		rows[1].set(A._12, A._22, A._32, A._42);
		rowCount = 2;
		break;
	case RC_3x4:
		rows[0].set(A._11, A._21, A._31, A._41);
		rows[1].set(A._12, A._22, A._32, A._42);
		rows[2].set(A._13, A._23, A._33, A._43);
		rowCount = 3;
		break;
	case RC_4x4:
		rows[0].set(A._11, A._21, A._31, A._41);
		rows[1].set(A._12, A._22, A._32, A._42);
		rows[2].set(A._13, A._23, A._33, A._43);
		rows[3].set(A._14, A._24, A._34, A._44);
		rowCount = 4;
		break;
	default:
#ifdef DEBUG
		Debug.fatal(DEBUG_INFO, "Invalid constant run-time-type for '%s'", *C->name);
#else
		NODEFAULT;
#endif
	}
	const u32 base = (u32)L.index + rowCount * lineSize * e;
	WriteChecked(base, rows, rowCount * lineSize);
}

IC void dx10ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fvector4& A)
{
	VERIFY(RC_float == C->type);
	VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);

	const u32 base = (u32)L.index + lineSize * e;
	WriteChecked(base, &A[0], lineSize);
}

IC void dx10ConstantBuffer::set_bytes(R_constant_load& L, const void* data, u32 bytes)
{
	VERIFY((u32)L.index + L.size <= m_uiBufferSize);
	BYTE* it = (BYTE*)RawData(L.index);
	const u32 copied = _min(bytes, L.size);
	const u32 tail = L.size - copied;
	if (!m_bChanged && ps_r4_cbuffer_skip_unchanged)
	{
		bool same = (copied == 0 || memcmp(it, data, copied) == 0);
		if (same && tail > 0)
		{
			for (u32 i = 0; i < tail; ++i)
			{
				if (it[copied + i] != 0)
				{
					same = false;
					break;
				}
			}
		}
		if (same)
			return;
	}
	if (copied > 0)
		CopyMemory(it, data, copied);
	if (tail > 0)
		ZeroMemory(it + copied, tail);
	m_bChanged = true;
}

IC void* dx10ConstantBuffer::AccessDirect(R_constant_load& L, u32 DataSize)
{
	VERIFY(L.index < (int)m_uiBufferSize);
	BYTE* res = ((BYTE*)m_pBufferData) + L.index;

	if ((u32)L.index + DataSize <= m_uiBufferSize)
	{
		m_bChanged = true;
		return res;
	}
	else return 0;
}

#endif	//	dx10ConstantBuffer_impl_included
