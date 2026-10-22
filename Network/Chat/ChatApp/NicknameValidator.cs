
//***************************************************************************
// NicknameValidator.cs : 닉네임 형식 사전 검증 (서버 NicknameValidation.h와 같은 규칙)
//
// 서버가 최종 판정하지만, 규칙을 어긴 닉네임을 서버까지 보내지 않도록 입력 단계에서
// 미리 걸러 사용자에게 바로 알려주기 위한 것입니다.
//  - 허용 문자: ASCII 영문/숫자/밑줄, 한글 완성형 음절(U+AC00~U+D7A3),
//    한글 호환 자모(U+3131~U+318E)
//  - 길이: 1~16글자(코드포인트 기준). 허용 문자에는 보조 평면 문자가 없으므로
//    UTF-16 문자 수와 같다. UTF-8 바이트 수는 최대 16*3=48이라 서버의
//    버퍼 한도(kNicknameBytes=50, NUL 포함)를 넘지 않는다.
//***************************************************************************

namespace ChatApp
{
    public static class NicknameValidator
    {
        public const int MaxLength = 16;

        // 코드포인트 하나가 닉네임에 허용되는 문자인지 판정한다.
        public static bool IsAllowedChar(char c)
        {
            if (c < 0x80)
                return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';

            if (c >= 0xAC00 && c <= 0xD7A3)   // 한글 완성형 음절
                return true;

            if (c >= 0x3131 && c <= 0x318E)   // 한글 호환 자모
                return true;

            return false;
        }

        // 닉네임 형식이 올바르면 true. 공백을 포함한 앞뒤 가공은 호출부 책임이다.
        public static bool IsValid(string nickname)
        {
            if (string.IsNullOrEmpty(nickname) || nickname.Length > MaxLength)
                return false;

            foreach (char c in nickname)
            {
                if (!IsAllowedChar(c))
                    return false;
            }
            return true;
        }
    }
}