#include "NetJson.h"

#include <cstddef>
#include <charconv>
#include <cmath>
#include <limits>
#include <string_view>

constexpr int NET_JSON_MAX_DEPTH = 128;
constexpr size_t NET_JSON_MAX_NODES = 262144;

static const char* const g_szHexDigits = "0123456789abcdef";

static bool IsJsonWhitespace(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r';
}

static int JsonHexDigitValue(char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'A' + 10;
	return -1;
}

static long long JsonClampToInt(double flValue)
{
	if (std::isnan(flValue))
		return 0;
	if (flValue >= 9223372036854775808.0)
		return std::numeric_limits<long long>::max();
	if (flValue <= -9223372036854775808.0)
		return std::numeric_limits<long long>::min();
	return static_cast<long long>(flValue);
}

static double JsonOverflowValue(std::string_view svToken)
{
	size_t nPos = 0;
	bool bNegative = false;
	if (nPos < svToken.size() && svToken[nPos] == '-')
	{
		bNegative = true;
		nPos++;
	}

	long long llExponent = 0;
	size_t nIntDigits = 0;
	size_t nDigitIndex = 0;
	size_t nFirstSignificant = 0;
	bool bPastPoint = false;
	bool bInExponent = false;
	bool bExpNegative = false;

	while (nPos < svToken.size())
	{
		const char ch = svToken[nPos++];
		if (ch == '.')
		{
			bPastPoint = true;
			continue;
		}
		if (ch == 'e' || ch == 'E')
		{
			bInExponent = true;
			if (nPos < svToken.size() && (svToken[nPos] == '+' || svToken[nPos] == '-'))
			{
				bExpNegative = svToken[nPos] == '-';
				nPos++;
			}
			continue;
		}
		if (ch < '0' || ch > '9')
			return std::numeric_limits<double>::infinity();
		if (bInExponent)
		{
			if (llExponent < 100000)
				llExponent = llExponent * 10 + (ch - '0');
			else
				llExponent = 100000;
			continue;
		}
		nDigitIndex++;
		if (ch != '0' && nFirstSignificant == 0)
			nFirstSignificant = nDigitIndex;
		if (!bPastPoint)
			nIntDigits = nDigitIndex;
	}

	long long llMagnitude = (bExpNegative ? -llExponent : llExponent) + (long long)nIntDigits - (long long)nFirstSignificant;

	if (nFirstSignificant == 0)
		return 0.0;
	if (llMagnitude >= 308)
		return bNegative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
	return 0.0;
}

static void JsonAppendUtf8(std::string& strOut, unsigned int nCodePoint)
{
	if (nCodePoint < 0x80)
	{
		strOut.push_back(static_cast<char>(nCodePoint));
		return;
	}
	if (nCodePoint < 0x800)
	{
		strOut.push_back(static_cast<char>(0xC0 | (nCodePoint >> 6)));
		strOut.push_back(static_cast<char>(0x80 | (nCodePoint & 0x3F)));
		return;
	}
	if (nCodePoint < 0x10000)
	{
		strOut.push_back(static_cast<char>(0xE0 | (nCodePoint >> 12)));
		strOut.push_back(static_cast<char>(0x80 | ((nCodePoint >> 6) & 0x3F)));
		strOut.push_back(static_cast<char>(0x80 | (nCodePoint & 0x3F)));
		return;
	}
	strOut.push_back(static_cast<char>(0xF0 | (nCodePoint >> 18)));
	strOut.push_back(static_cast<char>(0x80 | ((nCodePoint >> 12) & 0x3F)));
	strOut.push_back(static_cast<char>(0x80 | ((nCodePoint >> 6) & 0x3F)));
	strOut.push_back(static_cast<char>(0x80 | (nCodePoint & 0x3F)));
}

struct SJsonParser
{
	std::string_view m_svText;
	size_t m_nPos = 0;
	int m_nDepth = 0;
	size_t m_nNodes = 0;
	bool m_bFailed = false;

	explicit SJsonParser(const std::string& strText) : m_svText(strText) {}

	bool AtEnd() const
	{
		return m_nPos >= m_svText.size();
	}

	char Peek() const
	{
		return m_svText[m_nPos];
	}

	bool Consume(char chExpected)
	{
		if (m_nPos >= m_svText.size() || m_svText[m_nPos] != chExpected)
			return false;
		m_nPos++;
		return true;
	}

	void SkipWhitespace()
	{
		while (m_nPos < m_svText.size() && IsJsonWhitespace(m_svText[m_nPos]))
			m_nPos++;
	}

	bool ParseValue(CJsonValue& valueOut)
	{
		if (m_bFailed)
			return false;
		if (m_nNodes >= NET_JSON_MAX_NODES)
		{
			m_bFailed = true;
			return false;
		}
		m_nNodes++;
		SkipWhitespace();
		bool bOk = false;
		if (!AtEnd())
		{
			switch (Peek())
			{
			case '{': bOk = ParseObject(valueOut); break;
			case '[': bOk = ParseArray(valueOut); break;
			case '"': bOk = ParseString(valueOut); break;
			case 't': bOk = ParseLiteralValue(valueOut, "true", CJsonValue(true)); break;
			case 'f': bOk = ParseLiteralValue(valueOut, "false", CJsonValue(false)); break;
			case 'n': bOk = ParseLiteralValue(valueOut, "null", CJsonValue(nullptr)); break;
			default: bOk = ParseNumber(valueOut); break;
			}
		}
		if (!bOk)
			m_bFailed = true;
		return bOk;
	}

	bool ParseObject(CJsonValue& valueOut)
	{
		if (!Consume('{') || m_nDepth >= NET_JSON_MAX_DEPTH)
			return false;
		m_nDepth++;
		bool bOk = false;
		valueOut.Object();
		SkipWhitespace();
		if (Consume('}'))
			bOk = true;
		while (!bOk && !m_bFailed)
		{
			if (AtEnd() || Peek() != '"')
				break;
			std::string strKey;
			if (!ParseStringBody(strKey))
				break;
			SkipWhitespace();
			if (!Consume(':'))
				break;
			CJsonValue item;
			if (!ParseValue(item))
				break;
			valueOut.Set(strKey, std::move(item));
			SkipWhitespace();
			if (Consume(','))
			{
				SkipWhitespace();
				continue;
			}
			bOk = Consume('}');
		}
		m_nDepth--;
		return bOk;
	}

	bool ParseArray(CJsonValue& valueOut)
	{
		if (!Consume('[') || m_nDepth >= NET_JSON_MAX_DEPTH)
			return false;
		m_nDepth++;
		bool bOk = false;
		valueOut.Array();
		SkipWhitespace();
		if (Consume(']'))
			bOk = true;
		while (!bOk && !m_bFailed)
		{
			CJsonValue item;
			if (!ParseValue(item))
				break;
			valueOut.Push(std::move(item));
			SkipWhitespace();
			if (Consume(','))
			{
				SkipWhitespace();
				continue;
			}
			bOk = Consume(']');
		}
		m_nDepth--;
		return bOk;
	}

	bool ParseString(CJsonValue& valueOut)
	{
		std::string strValue;
		if (!ParseStringBody(strValue))
			return false;
		valueOut = CJsonValue(std::move(strValue));
		return true;
	}

	bool ParseStringBody(std::string& strOut)
	{
		if (!Consume('"'))
			return false;
		strOut.clear();
		while (true)
		{
			if (AtEnd())
				return false;
			const char ch = Peek();
			m_nPos++;
			if (ch == '"')
				return true;
			if (static_cast<unsigned char>(ch) < 0x20)
				return false;
			if (ch != '\\')
			{
				strOut.push_back(ch);
				continue;
			}
			if (AtEnd())
				return false;
			const char chEscape = Peek();
			m_nPos++;
			switch (chEscape)
			{
			case '"': strOut.push_back('"'); break;
			case '\\': strOut.push_back('\\'); break;
			case '/': strOut.push_back('/'); break;
			case 'b': strOut.push_back('\b'); break;
			case 'f': strOut.push_back('\f'); break;
			case 'n': strOut.push_back('\n'); break;
			case 'r': strOut.push_back('\r'); break;
			case 't': strOut.push_back('\t'); break;
			case 'u':
			{
				unsigned int nCode = 0;
				if (!ParseHexQuad(nCode))
					return false;
				if (nCode >= 0xD800 && nCode <= 0xDBFF)
				{
					if (m_svText.size() - m_nPos < 2 || m_svText[m_nPos] != '\\' || m_svText[m_nPos + 1] != 'u')
						return false;
					m_nPos += 2;
					unsigned int nLow = 0;
					if (!ParseHexQuad(nLow))
						return false;
					if (nLow < 0xDC00 || nLow > 0xDFFF)
						return false;
					nCode = 0x10000 + ((nCode - 0xD800) << 10) + (nLow - 0xDC00);
				}
				else if (nCode >= 0xDC00 && nCode <= 0xDFFF)
				{
					return false;
				}
				JsonAppendUtf8(strOut, nCode);
				break;
			}
			default:
				return false;
			}
		}
	}

	bool ParseHexQuad(unsigned int& nValueOut)
	{
		if (m_svText.size() - m_nPos < 4)
			return false;
		unsigned int nValue = 0;
		for (int i = 0; i < 4; i++)
		{
			const int nDigit = JsonHexDigitValue(m_svText[m_nPos + static_cast<size_t>(i)]);
			if (nDigit < 0)
				return false;
			nValue = (nValue << 4) | static_cast<unsigned int>(nDigit);
		}
		m_nPos += 4;
		nValueOut = nValue;
		return true;
	}

	bool ParseLiteralValue(CJsonValue& valueOut, std::string_view svLiteral, CJsonValue valueResult)
	{
		if (m_svText.size() - m_nPos < svLiteral.size() || m_svText.compare(m_nPos, svLiteral.size(), svLiteral) != 0)
			return false;
		m_nPos += svLiteral.size();
		valueOut = std::move(valueResult);
		return true;
	}

	bool ParseNumber(CJsonValue& valueOut)
	{
		const size_t nStart = m_nPos;
		Consume('-');
		if (AtEnd())
			return false;
		const char chFirst = Peek();
		if (chFirst == '0')
		{
			m_nPos++;
		}
		else if (chFirst >= '1' && chFirst <= '9')
		{
			while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
				m_nPos++;
		}
		else
		{
			return false;
		}
		if (!AtEnd() && Peek() == '.')
		{
			m_nPos++;
			if (AtEnd() || Peek() < '0' || Peek() > '9')
				return false;
			while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
				m_nPos++;
		}
		if (!AtEnd() && (Peek() == 'e' || Peek() == 'E'))
		{
			m_nPos++;
			if (!AtEnd() && (Peek() == '+' || Peek() == '-'))
				m_nPos++;
			if (AtEnd() || Peek() < '0' || Peek() > '9')
				return false;
			while (!AtEnd() && Peek() >= '0' && Peek() <= '9')
				m_nPos++;
		}
		const std::string_view svToken = m_svText.substr(nStart, m_nPos - nStart);
		bool bIntegral = true;
		for (const char ch : svToken)
		{
			if (ch == '.' || ch == 'e' || ch == 'E')
			{
				bIntegral = false;
				break;
			}
		}
		if (bIntegral)
		{
			long long llValue = 0;
			const auto result = std::from_chars(svToken.data(), svToken.data() + svToken.size(), llValue);
			if (result.ec == std::errc() && result.ptr == svToken.data() + svToken.size())
			{
				valueOut = CJsonValue(llValue);
				return true;
			}
		}
		double flValue = 0.0;
		const auto result = std::from_chars(svToken.data(), svToken.data() + svToken.size(), flValue);
		if (result.ptr != svToken.data() + svToken.size())
			return false;
		if (result.ec == std::errc::result_out_of_range)
			flValue = JsonOverflowValue(svToken);
		else if (result.ec != std::errc())
			return false;
		valueOut = CJsonValue(flValue);
		return true;
	}
};

CJsonValue::CJsonValue(std::nullptr_t) : m_eType(EType::NULLVALUE) {}

CJsonValue::CJsonValue(bool bValue) : m_eType(EType::BOOLEAN), m_bBool(bValue) {}

CJsonValue::CJsonValue(int nValue) : m_eType(EType::NUMBER), m_bInteger(true), m_llNumber(nValue), m_flNumber(static_cast<double>(nValue)) {}

CJsonValue::CJsonValue(long long llValue) : m_eType(EType::NUMBER), m_bInteger(true), m_llNumber(llValue), m_flNumber(static_cast<double>(llValue)) {}

CJsonValue::CJsonValue(double flValue) : m_eType(EType::NUMBER), m_llNumber(JsonClampToInt(flValue)), m_flNumber(flValue) {}

CJsonValue::CJsonValue(const char* szValue) : m_eType(EType::STRING), m_strString(szValue ? szValue : "") {}

CJsonValue::CJsonValue(std::string strValue) : m_eType(EType::STRING), m_strString(std::move(strValue)) {}

bool CJsonValue::GetBool(bool bDefault) const
{
	return m_eType == EType::BOOLEAN ? m_bBool : bDefault;
}

double CJsonValue::GetNumber(double flDefault) const
{
	return m_eType == EType::NUMBER ? m_flNumber : flDefault;
}

long long CJsonValue::GetInt(long long llDefault) const
{
	return m_eType == EType::NUMBER ? m_llNumber : llDefault;
}

std::string CJsonValue::GetString(const std::string& strDefault) const
{
	return m_eType == EType::STRING ? m_strString : strDefault;
}

const std::vector<CJsonValue>& CJsonValue::GetArray() const
{
	static const std::vector<CJsonValue> vecEmpty;
	return m_eType == EType::ARRAY ? m_vecArray : vecEmpty;
}

const std::map<std::string, CJsonValue>& CJsonValue::GetObject() const
{
	static const std::map<std::string, CJsonValue> mapEmpty;
	return m_eType == EType::OBJECT ? m_mapObject : mapEmpty;
}

const CJsonValue* CJsonValue::Find(const std::string& strKey) const
{
	if (m_eType != EType::OBJECT)
		return nullptr;
	const auto it = m_mapObject.find(strKey);
	return it != m_mapObject.end() ? &it->second : nullptr;
}

bool CJsonValue::HasKey(const std::string& strKey) const
{
	return Find(strKey) != nullptr;
}

std::vector<CJsonValue>& CJsonValue::Array()
{
	if (m_eType != EType::ARRAY)
	{
		m_eType = EType::ARRAY;
		m_bBool = false;
		m_bInteger = false;
		m_llNumber = 0;
		m_flNumber = 0.0;
		m_strString.clear();
		m_mapObject.clear();
	}
	return m_vecArray;
}

std::map<std::string, CJsonValue>& CJsonValue::Object()
{
	if (m_eType != EType::OBJECT)
	{
		m_eType = EType::OBJECT;
		m_bBool = false;
		m_bInteger = false;
		m_llNumber = 0;
		m_flNumber = 0.0;
		m_strString.clear();
		m_vecArray.clear();
	}
	return m_mapObject;
}

void CJsonValue::Set(const std::string& strKey, CJsonValue value)
{
	Object()[strKey] = std::move(value);
}

void CJsonValue::Push(CJsonValue value)
{
	Array().push_back(std::move(value));
}

std::string CJsonValue::Dump() const
{
	return NetJson::Write(*this);
}

CJsonValue CJsonValue::Parse(const std::string& strText, bool* pbOk)
{
	SJsonParser parser(strText);
	CJsonValue value;
	bool bOk = parser.ParseValue(value);
	if (bOk)
	{
		parser.SkipWhitespace();
		if (!parser.AtEnd())
			bOk = false;
	}
	if (pbOk)
		*pbOk = bOk;
	if (!bOk)
		return CJsonValue();
	return value;
}

CJsonValue NetJson::Object()
{
	CJsonValue value;
	value.Object();
	return value;
}

CJsonValue NetJson::Array()
{
	CJsonValue value;
	value.Array();
	return value;
}

CJsonValue NetJson::MakeObject(std::initializer_list<std::pair<const char*, CJsonValue>> vecPairs)
{
	CJsonValue value;
	value.Object();
	for (const auto& pair : vecPairs)
	{
		if (pair.first)
			value.Set(pair.first, pair.second);
	}
	return value;
}

CJsonValue NetJson::MakeArray(std::initializer_list<CJsonValue> vecItems)
{
	CJsonValue value;
	value.Array();
	for (const auto& item : vecItems)
		value.Push(item);
	return value;
}

CJsonValue NetJson::Parse(const std::string& strText, bool* pbOk)
{
	return CJsonValue::Parse(strText, pbOk);
}

static std::string WriteNumberValue(const CJsonValue& value)
{
	const double flValue = value.GetNumber();
	if (!std::isfinite(flValue))
		return "null";
	char szBuffer[64] = {};
	if (value.IsInteger())
	{
		const long long llValue = value.GetInt();
		if (static_cast<double>(llValue) == flValue)
		{
			const auto result = std::to_chars(szBuffer, szBuffer + sizeof(szBuffer), llValue);
			return std::string(szBuffer, result.ptr);
		}
	}
	const auto result = std::to_chars(szBuffer, szBuffer + sizeof(szBuffer), flValue);
	return std::string(szBuffer, result.ptr);
}

static std::string WriteValue(const CJsonValue& value, int nDepth)
{
	switch (value.GetType())
	{
	case CJsonValue::EType::NULLVALUE:
		return "null";
	case CJsonValue::EType::BOOLEAN:
		return value.GetBool() ? "true" : "false";
	case CJsonValue::EType::NUMBER:
		return WriteNumberValue(value);
	case CJsonValue::EType::STRING:
		return "\"" + NetJson::Escape(value.GetString()) + "\"";
	case CJsonValue::EType::ARRAY:
	{
		if (nDepth >= NET_JSON_MAX_DEPTH)
			return "null";
		std::string strOut = "[";
		const auto& vecArray = value.GetArray();
		for (size_t i = 0; i < vecArray.size(); i++)
		{
			if (i)
				strOut += ",";
			strOut += WriteValue(vecArray[i], nDepth + 1);
		}
		strOut += "]";
		return strOut;
	}
	case CJsonValue::EType::OBJECT:
	{
		if (nDepth >= NET_JSON_MAX_DEPTH)
			return "null";
		std::string strOut = "{";
		const auto& mapObject = value.GetObject();
		bool bFirst = true;
		for (const auto& pair : mapObject)
		{
			if (!bFirst)
				strOut += ",";
			bFirst = false;
			strOut += "\"";
			strOut += NetJson::Escape(pair.first);
			strOut += "\":";
			strOut += WriteValue(pair.second, nDepth + 1);
		}
		strOut += "}";
		return strOut;
	}
	}
	return "null";
}

std::string NetJson::Write(const CJsonValue& value)
{
	return WriteValue(value, 0);
}

std::string NetJson::Escape(const std::string& strText)
{
	std::string strOut;
	strOut.reserve(strText.size() + 2);
	for (const char ch : strText)
	{
		switch (ch)
		{
		case '"': strOut += "\\\""; break;
		case '\\': strOut += "\\\\"; break;
		case '\b': strOut += "\\b"; break;
		case '\f': strOut += "\\f"; break;
		case '\n': strOut += "\\n"; break;
		case '\r': strOut += "\\r"; break;
		case '\t': strOut += "\\t"; break;
		default:
		{
			const unsigned char uc = static_cast<unsigned char>(ch);
			if (uc < 0x20)
			{
				strOut += "\\u00";
				strOut.push_back(g_szHexDigits[(uc >> 4) & 0xF]);
				strOut.push_back(g_szHexDigits[uc & 0xF]);
			}
			else
			{
				strOut.push_back(ch);
			}
			break;
		}
		}
	}
	return strOut;
}
