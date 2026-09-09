
//***************************************************************************
// AccountStorage.cs : 로컬 계정 파일(public_id+token) 저장/로드.
//
// C++ 콘솔 클라이언트(ChatClientMain.cpp)와 같은 아이디어 — "프로필 이름"
// (Connect()에 넘긴 값)을 키로 로컬 파일에 public_id+token을 저장해 다음
// 실행 시 재접속에 쓴다. 파일 형식이 C++ 클라이언트와 바이트 단위로
// 호환될 필요는 없다(각자 자기 클라이언트로만 읽고 쓴다는 전제) — 다만
// "1행: public_id 16진, 2행: token 16진" 구조는 이해하기 쉽게 동일한
// 아이디어로 맞췄다.
//
// [수정 — 작업 디렉터리에 따라 재접속이 안 되던 문제] 예전엔 "chat_token_..."
// 상대 경로를 그대로 썼다. 상대 경로는 "현재 작업 디렉터리(CWD)"를
// 기준으로 풀리는데, CWD는 실행 파일이 실제로 위치한 폴더와 다를 수
// 있다 — 특히 Visual Studio에서 F5로 디버그 실행할 때 CWD가 프로젝트
// 폴더로 잡히는 경우가 흔한데, post-build 이벤트(xcopy)로 복사된 exe를
// 다른 폴더에서 직접 실행하면 그때는 CWD가 exe 자신의 폴더가 된다 —
// 서로 다른 실행 방식이 서로 다른 실제 경로를 가리키다 보니, 한쪽에서
// 저장한 계정 파일을 다른 쪽에서 절대 찾지 못해 매번 재접속 대신 신규
// 가입을 재시도하다 실패하는 증상이 났다.
//
// AppContext.BaseDirectory는 CWD가 아니라 "이 실행 파일(및 딸린 DLL들)이
// 실제로 위치한 폴더"를 가리키는 별개의 값이다 — 어떻게 실행되든
// (더블클릭, VS 디버그, 다른 위치로 복사 후 실행 등) 항상 그 exe 자신의
// 폴더를 정확히 가리키므로 실행 방식에 따라 값이 흔들리지 않는다.
//
// [알려진 한계] 파일이 평문(16진 텍스트)으로 저장된다 — 같은 PC의 다른
// 사용자/프로세스가 파일에 접근할 수 있는 환경이라면 안전하지 않다.
// 실서비스에서는 Windows DPAPI(System.Security.Cryptography.ProtectedData)로
// 암호화해 저장하는 것을 권장한다(이 데모엔 미적용 — C++ 클라이언트와
// 동일한 트레이드오프). 또한 실행 파일이 관리자 권한이 있어야만 쓸 수
// 있는 폴더(예: Program Files)에 설치되는 배포 형태라면 이 폴더에
// 쓰기가 실패할 수 있다 — 그런 경우엔 %APPDATA% 등 사용자 쓰기 가능한
// 경로로 다시 바꿔야 한다.
//***************************************************************************

using System;
using System.IO;
using System.Text;

namespace ChatApp
{
    public static class AccountStorage
    {
        //***************************************************************************
        // @brief 계정 파일들을 저장할 폴더 경로를 반환합니다 — 실행 파일(및 딸린
        //        DLL들)이 실제로 위치한 폴더. 실행 시점의 현재 디렉터리와는 무관하다.
        //***************************************************************************
        private static string GetStorageDirectory() => AppContext.BaseDirectory;

        private static string PathFor(string profileName) =>
            Path.Combine(GetStorageDirectory(), "chat_token_" + profileName + ".dat");

        //***************************************************************************
        // @brief 이 프로필 이름에 대응하는 실제 파일 경로를 조회합니다(진단용).
        // @details 로그인 시도 전에 화면에 찍어 보여주면, "지금 어느 파일을
        //          찾고 있는지"를 바로 확인할 수 있다 — 저장 위치를 바꾼
        //          직후처럼 "예전에 저장된 파일이 새 위치에 없어서 매번
        //          신규 가입으로 처리되는" 상황을 진단하기 쉬워진다.
        //***************************************************************************
        public static string GetAccountFilePath(string profileName) => PathFor(profileName);

        //***************************************************************************
        // @brief 로컬 계정 파일을 읽습니다. 파일이 없거나 형식이 잘못됐으면 false.
        //***************************************************************************
        public static bool TryLoad(string profileName, out byte[] publicId, out byte[] token)
        {
            publicId = null;
            token = null;

            string path = PathFor(profileName);
            if (!File.Exists(path))
                return false;

            string[] lines = File.ReadAllLines(path);
            if (lines.Length < 2)
                return false;

            publicId = HexToBytes(lines[0]);
            token = HexToBytes(lines[1]);

            return publicId != null && token != null
                && publicId.Length == ProtocolConstants.PublicIdBytes
                && token.Length == ProtocolConstants.TokenBytes;
        }

        //***************************************************************************
        // @brief 로컬 계정 파일에 저장(덮어쓰기)합니다.
        //***************************************************************************
        public static void Save(string profileName, byte[] publicId, byte[] token)
        {
            string path = PathFor(profileName);
            File.WriteAllText(path, BytesToHex(publicId) + "\n" + BytesToHex(token));
        }

        private static string BytesToHex(byte[] bytes)
        {
            var sb = new StringBuilder(bytes.Length * 2);
            foreach (byte b in bytes)
                sb.Append(b.ToString("x2"));
            return sb.ToString();
        }

        private static byte[] HexToBytes(string hex)
        {
            hex = hex.Trim();
            if (hex.Length % 2 != 0)
                return null;

            try
            {
                byte[] bytes = new byte[hex.Length / 2];
                for (int i = 0; i < bytes.Length; i++)
                    bytes[i] = Convert.ToByte(hex.Substring(i * 2, 2), 16);
                return bytes;
            }
            catch
            {
                return null; // 잘못된 16진 문자열
            }
        }
    }
}