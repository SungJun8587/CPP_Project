
//***************************************************************************
// Character.h : interface for the CCharacter class.
//
//***************************************************************************

#ifndef __CHARACTER_H__
#define __CHARACTER_H__

//***************************************************************************
//
class CCharacter
{
public:
	CCharacter();
	virtual ~CCharacter();

	void		SetCharacter(int nCharacter);
	int			GetCharacter() { return m_nCharacter; }
	void        SetExpression(int nExpression);
	int			GetExpression() { return m_nExpression; }

	BOOL		SetEquipments(int* pnItemImageInfo, int nNum, CItemInfoList*& pItemInfoList);
	BOOL		GetCharacterInfo(int nChrNumber, CCharacterInfo& CharacterInfo);
	BOOL		GetItemImageInfo(int nItemNumber, CItemInfoList*& pItemInfoList);
	BOOL		GetExprImageInfo(int nChrNumber, int nExprNumber, CExpressionInfo& ExpressionInfo);

private:
	int			m_nCharacter;
	int			m_nExpression;
};

#endif // ndef __CHARACTER_H__