-- ***************************************************************************
-- create_chat_db.sql : 채팅 서버 회원 DB 스키마 생성 스크립트 (MySQL)
--
-- AccountDBHandler.cpp / ChangeNicknameDBHandler.cpp가 가정하는 스키마와
-- 정확히 일치합니다:
--   - uid(내부 전용, AUTO_INCREMENT)를 PRIMARY KEY로 사용
--   - public_id(16바이트 무작위값을 16진 소문자 32자로 인코딩해 저장)를 외부 노출용 안정 식별자로 사용
--     (닉네임 변경과 무관하게 고정 — Redis 키/클라이언트 로컬 토큰 파일이
--     이 값을 기준으로 함)
--   - nickname은 이제 PK가 아니라 UNIQUE 제약만 걸린 표시용 컬럼
--     (INSERT/UPDATE 실패 시 중복 판별 근거는 이 UNIQUE 제약)
--   - token_hash는 Crypto::CCryptoUtil::HashSHA256()의 출력(16진 소문자, 64자)을 그대로 저장
--   - 토큰 원문은 DB에 저장하지 않음 — 해시만 저장(CryptoUtil.h/AccountDBHandler.cpp 설계 참고)
-- ***************************************************************************

CREATE DATABASE IF NOT EXISTS chat_db
	CHARACTER SET utf8mb4
	COLLATE utf8mb4_unicode_ci;

USE chat_db;


DROP TABLE IF EXISTS users;

-- ---------------------------------------------------------------------------
-- users : uid = 내부 전용 순번 PK, public_id = 외부 노출용 안정 식별자,
--         nickname = 표시용(변경 가능), token_hash = 재접속 인증용 회전 토큰의 해시
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users
(
	-- 내부 전용 순번 PK. 클러스터드 인덱스/JOIN/FK 등 DB 내부 용도로만
	-- 쓰고, 외부(패킷/로그/URL 등)에는 절대 노출하지 않는다 — 순번이라
	-- 그대로 노출하면 가입자 수 추정/열거 공격에 쓰일 수 있음.
	uid          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '내부 전용 순번 PK. 외부에 노출 금지',

	-- 외부에 노출되는 계정의 실질적 식별자(AWS의 Access Key ID와 유사한
	-- 위치 — 비밀은 아니지만 순번처럼 추측 가능하지도 않음). 가입 시
	-- Crypto::CCryptoUtil::GenerateRandomBytes()로 생성하는 진짜 난수 16바이트.
	-- 닉네임이 바뀌어도 이 값은 절대 바뀌지 않는다 — Redis 키/클라이언트
	-- 로컬 토큰 파일이 이 값을 기준으로 하므로, 닉네임 변경 시 그런
	-- 부수 자원들을 이전(rename)할 필요가 원천적으로 없어진다.
	public_id    CHAR(32)     NOT NULL COMMENT '외부 노출용 안정 식별자(진짜 난수 16바이트를 16진 소문자 32자로 인코딩). 닉네임 변경과 무관하게 고정. token_hash와 동일한 이유(ODBC 바이너리 파라미터 바인딩의 드라이버별 불확실성 회피)로 BINARY(16) 대신 문자열로 저장',

	-- 닉네임. 코드 상 IsValidNickname()이 아스키 영문/숫자/밑줄 또는
	-- 한글(완성형 음절/호환 자모), 1~16글자로 제한한다. 테이블 문자셋
	-- 자체는 utf8mb4라 한글을 포함한 다국어 표시가 가능하다. 더 이상
	-- PK가 아니므로 ChangeNicknameDBHandler.cpp가 자유롭게 UPDATE할 수
	-- 있다(대상 행은 public_id로 찾음).
	nickname     VARCHAR(16)  NOT NULL COMMENT '닉네임(표시용, 변경 가능). 영문/숫자/밑줄/한글, 1~16글자만 허용(NicknameValidation.h)',

	-- SHA-256 해시의 16진 문자열 표현 = 항상 정확히 64자(32바이트 * 2).
	-- 토큰 원문은 어디에도 저장하지 않는다.
	token_hash   CHAR(64)     NOT NULL COMMENT '재접속 토큰의 SHA-256 해시(16진, 64자). 원문 토큰은 저장하지 않음. 로그인마다 회전(재발급)됨',

	created_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '최초 가입(INSERT) 시각',
	updated_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '토큰 회전/닉네임 변경 등으로 마지막 갱신된 시각',

	PRIMARY KEY (uid),
	UNIQUE KEY uk_users_public_id (public_id),
	UNIQUE KEY uk_users_nickname (nickname)
)
ENGINE = InnoDB
DEFAULT CHARSET = utf8mb4
COLLATE = utf8mb4_unicode_ci
COMMENT = '채팅 서버 회원 계정';

-- ---------------------------------------------------------------------------
-- (선택) 애플리케이션 접속 계정 생성 — demo(ChatServer.cpp)의 CDBNode 설정과
-- 맞추려면 아래 계정 정보를 그대로 쓰거나, 실제 운영 값으로 바꿔서 실행하세요.
-- 이미 계정이 있다면 이 블록은 건너뛰어도 됩니다. DB 관리자 권한 필요.
-- ---------------------------------------------------------------------------
-- CREATE USER IF NOT EXISTS 'chat_user'@'%' IDENTIFIED BY 'chat_password';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.users TO 'chat_user'@'%';
-- FLUSH PRIVILEGES;