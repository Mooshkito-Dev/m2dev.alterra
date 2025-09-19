#include "StdAfx.h"
#include "EffectElementBase.h"


void CEffectElementBase::GetPosition(float fTime, D3DXVECTOR3 & rPosition)
{
	rPosition = GetTimeEventBlendValue(fTime, m_TimeEventTablePosition);
}

/*
bool CEffectElementBase::isVisible(float fTime)
{
	for (DWORD i = 0; i < m_TimeEventTableVisible.size(); ++i)
	{
		float fPointTime = m_TimeEventTableVisible[i];

		if (fTime < fPointTime)
		{
			if (1 == i % 2)
				return true;
			else
				return false;
		}
	}

	return 1 == (m_TimeEventTableVisible.size() % 2);
}

void CEffectElementBase::GetAlpha(float fTime, float * pAlpha)
{
	GetTimeEventBlendValue<TTimeEventTableFloat, float>(fTime, m_TimeEventAlpha, pAlpha);
}

void CEffectElementBase::GetScale(float fTime, float * pScale)
{
	GetTimeEventBlendValue<TTimeEventTableFloat, float>(fTime, m_TimeEventScale, pScale);
}
*/

bool CEffectElementBase::isData()
{
	return OnIsData();
}

void CEffectElementBase::Clear()
{
	m_fStartTime = 0.0f;

	OnClear();
}

BOOL CEffectElementBase::LoadScript(CTextFileLoader & rTextFileLoader)
{
	CTokenVector * pTokenVector;
	if (!rTextFileLoader.GetTokenFloat("starttime",&m_fStartTime))
	{
		m_fStartTime = 0.0f;
	}
	if (rTextFileLoader.GetTokenVector("timeeventposition", &pTokenVector))
	{	
		m_TimeEventTablePosition.clear();
		
		DWORD dwIndex = 0;
		for (DWORD i = 0; i < pTokenVector->size(); ++dwIndex)
		{
			TEffectPosition EffectPosition;
			EffectPosition.m_fTime = atof(pTokenVector->at(i++).c_str());
			if (pTokenVector->at(i)=="MOVING_TYPE_BEZIER_CURVE")
			{
				i++;

				EffectPosition.m_iMovingType = MOVING_TYPE_BEZIER_CURVE;

				EffectPosition.m_Value.x = atof(pTokenVector->at(i++).c_str());
				EffectPosition.m_Value.y = atof(pTokenVector->at(i++).c_str());
				EffectPosition.m_Value.z = atof(pTokenVector->at(i++).c_str());

				EffectPosition.m_vecControlPoint.x = atof(pTokenVector->at(i++).c_str());
				EffectPosition.m_vecControlPoint.y = atof(pTokenVector->at(i++).c_str());
				EffectPosition.m_vecControlPoint.z = atof(pTokenVector->at(i++).c_str());
			}
			else if (pTokenVector->at(i) == "MOVING_TYPE_DIRECT")
			{
				i++;

				EffectPosition.m_iMovingType = MOVING_TYPE_DIRECT;

				EffectPosition.m_Value.x = atof(pTokenVector->at(i++).c_str());
				EffectPosition.m_Value.y = atof(pTokenVector->at(i++).c_str());
				EffectPosition.m_Value.z = atof(pTokenVector->at(i++).c_str());

				EffectPosition.m_vecControlPoint = D3DXVECTOR3(0.0f,0.0f,0.0f);
			}
			else
			{
				return FALSE;
			}

			m_TimeEventTablePosition.push_back(EffectPosition);
		}
	}	
	
	return OnLoadScript(rTextFileLoader);
}

float CEffectElementBase::GetStartTime()
{
	return m_fStartTime;
}

CEffectElementBase::CEffectElementBase()
{
	m_fStartTime = 0.0f;
}
CEffectElementBase::~CEffectElementBase()
{
}
