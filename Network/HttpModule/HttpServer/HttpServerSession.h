//***************************************************************************
// HttpServerSession.h : interface for the CHttpServerSession class.
//
//***************************************************************************

#ifndef __HTTPSERVERSESSION_H__
#define __HTTPSERVERSESSION_H__

#ifndef __IOCPSESSION_H__
#include <Network/IOCP/IocpSession.h>
#endif

#ifndef __HTTPFORMUTIL_H__
#include <Network/HTTP/HttpFormUtil.h>
#endif

#ifndef __HTTPREQUESTPARSER_H__
#include <Network/HTTP/HttpRequestParser.h>
#endif

#ifndef __ENCODINGCONVERT_H__
#include <Util/EncodingConvert.h>
#endif

#include <string>
#include <string_view>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <algorithm>
#include <cctype>

//***************************************************************************
// @struct StaticFileResult
// @brief CHttpServerSession::ServeStaticFile()이 반환하는 결과
// @details 파일 내용을 그대로 담은 소유 값이며, 상위 계층에서 응답 전송 시 사용할 수 있습니다.
//***************************************************************************
struct StaticFileResult
{
    bool        found = false;        // 파일을 정상적으로 읽었는지 여부 (false면 statusCode로 원인 구분)
    int         statusCode = 404;     // 200(성공) / 404(없음) / 403(경로 위반, 디렉토리 등)
    std::string contentType;          // 확장자로 결정된 Content-Type (찾은 경우만 의미 있음)
    std::string body;                 // 파일 내용 (바이너리 그대로 — 이미지/PDF 등도 손상 없이 담김)
};

//***************************************************************************
// @class CHttpServerSession
// @brief 설정 가능한 루트 디렉토리 아래의 정적 파일(HTML/JSON/XML/이미지 등)을 읽어 HTTP 응답용 결과로 돌려주는 서버 컴포넌트.
// @details
//      [경로 하드코딩 없음] 루트 디렉토리는 생성자 인자로 주입한다 — 코드
//      어디에도 고정 경로가 없다. 여러 루트를 동시에 쓰고 싶으면(예: 정적
//      자산용/업로드 파일용을 분리) 인스턴스를 여러 개 만들면 된다.
//
//      [확장자 기반 Content-Type] 기본 매핑 테이블에 .json/.xml을 포함한
//      일반적인 정적 파일 확장자가 들어있고(DefaultMimeTypes() 참고),
//      SetMimeType()으로 프로젝트별 확장자를 추가/덮어쓸 수 있다. 매핑에
//      없는 확장자는 "application/octet-stream"으로 응답한다(파일 자체는
//      정상적으로 읽어서 보냄 — Content-Type만 범용값).
//
//      [디렉토리 트래버설 방어] 요청 경로를 percent-decoding한 뒤 루트
//      디렉토리와 결합해서 std::filesystem::weakly_canonical()로 정규화하고,
//      그 결과가 정규화된 루트 디렉토리로 "시작하는지"를 확인한다 — 예를 들어
//      "/../../etc/passwd" 같은 요청이 결합·정규화 과정에서 루트 밖으로
//      벗어나면 403으로 거부하고 실제 파일 읽기를 시도하지 않는다.
//
//      [스레드 안전성] _mimeTypes를 생성 이후에 SetMimeType()으로 변경하는
//      작업과 ServeStaticFile() 호출을 여러 스레드에서 동시에 하면 데이터 레이스가
//      될 수 있다 — 보통 서버 시작 시점에 SetMimeType()으로 설정을 다 끝내고
//      그 이후로는 ServeStaticFile()만 호출하는 패턴을 권장한다(그 경우 unordered_map
//      은 읽기 전용 동시 접근에 안전).
//***************************************************************************
class CHttpServerSession : public CIocpSession
{
public:
    explicit CHttpServerSession(std::string rootDirectory = "Statics");
    virtual ~CHttpServerSession() = default;

    void SetMimeType(std::string extension, std::string contentType);
    StaticFileResult ServeStaticFile(std::string_view requestPath) const;

protected:
    virtual void OnConnected() override;
    virtual int32 OnRecv(BYTE* buffer, int32 len) override;

private:
    void ProcessHttpRequest();
    void SendTextResponse(const _tstring& status, const _tstring& contentType, const _tstring& body);
    void SendBinaryResponse(const _tstring& status, const _tstring& contentType, const std::vector<BYTE>& body);
    void SendResponseString(const _tstring& str);

    bool ResolveSafePath(std::string_view requestPath, std::filesystem::path& outPath) const;
    std::string LookupContentType(const std::filesystem::path& path) const;
    static std::unordered_map<std::string, std::string> DefaultMimeTypes();

private:
    CHttpRequestParser                           _parser;         // HTTP 요청 파싱을 담당하는 파서 객체
    std::string                                  _rootDirectory;  // 정적 파일 기본 루트 디렉터리 경로
    std::unordered_map<std::string, std::string> _mimeTypes;      // 확장자(소문자, 점 포함) -> Content-Type 매핑 테이블
};

#endif // ndef __HTTPSERVERSESSION_H__