#pragma once

#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <vector>

class CJsonValue
{
public:
	enum class EType { NULLVALUE, BOOLEAN, NUMBER, STRING, ARRAY, OBJECT };

	CJsonValue() = default;
	CJsonValue(std::nullptr_t);
	CJsonValue(bool bValue);
	CJsonValue(int nValue);
	CJsonValue(long long llValue);
	CJsonValue(double flValue);
	CJsonValue(const char* szValue);
	CJsonValue(std::string strValue);

	EType GetType() const { return m_eType; }
	bool IsNull() const { return m_eType == EType::NULLVALUE; }
	bool IsBoolean() const { return m_eType == EType::BOOLEAN; }
	bool IsNumber() const { return m_eType == EType::NUMBER; }
	bool IsInteger() const { return m_eType == EType::NUMBER && m_bInteger; }
	bool IsString() const { return m_eType == EType::STRING; }
	bool IsArray() const { return m_eType == EType::ARRAY; }
	bool IsObject() const { return m_eType == EType::OBJECT; }

	bool GetBool(bool bDefault = false) const;
	double GetNumber(double flDefault = 0.0) const;
	long long GetInt(long long llDefault = 0) const;
	std::string GetString(const std::string& strDefault = {}) const;
	const std::vector<CJsonValue>& GetArray() const;
	const std::map<std::string, CJsonValue>& GetObject() const;

	const CJsonValue* Find(const std::string& strKey) const;
	bool HasKey(const std::string& strKey) const;

	std::vector<CJsonValue>& Array();
	std::map<std::string, CJsonValue>& Object();
	void Set(const std::string& strKey, CJsonValue value);
	void Push(CJsonValue value);

	std::string Dump() const;

	static CJsonValue Parse(const std::string& strText, bool* pbOk = nullptr);

private:
	EType m_eType = EType::NULLVALUE;
	bool m_bBool = false;
	bool m_bInteger = false;
	long long m_llNumber = 0;
	double m_flNumber = 0.0;
	std::string m_strString;
	std::vector<CJsonValue> m_vecArray;
	std::map<std::string, CJsonValue> m_mapObject;
};

namespace NetJson
{
	CJsonValue Object();
	CJsonValue Array();
	CJsonValue MakeObject(std::initializer_list<std::pair<const char*, CJsonValue>> vecPairs);
	CJsonValue MakeArray(std::initializer_list<CJsonValue> vecItems);
	CJsonValue Parse(const std::string& strText, bool* pbOk);
	std::string Write(const CJsonValue& value);
	std::string Escape(const std::string& strText);
}
