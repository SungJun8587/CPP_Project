-- ***************************************************************************
-- create_chat_db.sql : 채팅 서버 회원 DB 스키마 생성 스크립트 (MySQL)
--
-- AccountDBHandler.cpp가 가정하는 스키마와 정확히 일치합니다:
--   - nickname을 PRIMARY KEY로 사용 (INSERT 실패 시 중복 판별 근거)
--   - token_hash는 Crypto::CCryptoUtil::HashSHA256()의 출력(16진 소문자, 64자)을 그대로 저장
--   - 토큰 원문은 DB에 저장하지 않음 — 해시만 저장(CryptoUtil.h/AccountDBHandler.cpp 설계 참고)
-- ***************************************************************************

CREATE DATABASE IF NOT EXISTS chat
	CHARACTER SET utf8mb4
	COLLATE utf8mb4_unicode_ci;

USE chat;

-- ---------------------------------------------------------------------------
-- users : 닉네임 = 계정 식별자, token_hash = 재접속 인증용 회전 토큰의 해시
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users
(
	-- 닉네임. 코드 상 IsValidNickname()이 영문/숫자/밑줄, 1~31자로 이미
	-- 제한하므로 ASCII로 충분하지만, 테이블 문자셋 자체는 utf8mb4로 둬서
	-- 향후 검증 규칙이 완화돼도 스키마 변경 없이 대응 가능하게 함.
	nickname     VARCHAR(31)  NOT NULL COMMENT '닉네임(계정 식별자). 영문/숫자/밑줄, 1~31자만 허용(AccountDBHandler::IsValidNickname)',

	-- SHA-256 해시의 16진 문자열 표현 = 항상 정확히 64자(32바이트 * 2).
	-- 토큰 원문은 어디에도 저장하지 않는다.
	token_hash   CHAR(64)     NOT NULL COMMENT '재접속 토큰의 SHA-256 해시(16진, 64자). 원문 토큰은 저장하지 않음. 로그인마다 회전(재발급)됨',

	created_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '최초 가입(INSERT) 시각',
	updated_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP COMMENT '토큰 회전 등으로 마지막 갱신된 시각',

	PRIMARY KEY (nickname)
)
ENGINE = InnoDB
DEFAULT CHARSET = utf8mb4
COLLATE = utf8mb4_unicode_ci
COMMENT = '채팅 서버 회원 계정 — 닉네임 기반 가입/재접속 토큰 인증(AccountDBHandler.cpp)';

-- ---------------------------------------------------------------------------
-- (선택) 애플리케이션 접속 계정 생성 — demo(ChatServer.cpp)의 CDBNode 설정과
-- 맞추려면 아래 계정 정보를 그대로 쓰거나, 실제 운영 값으로 바꿔서 실행하세요.
-- 이미 계정이 있다면 이 블록은 건너뛰어도 됩니다. DB 관리자 권한 필요.
-- ---------------------------------------------------------------------------
-- CREATE USER IF NOT EXISTS 'chat_user'@'%' IDENTIFIED BY 'chat_password';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.users TO 'chat_user'@'%';
-- FLUSH PRIVILEGES;