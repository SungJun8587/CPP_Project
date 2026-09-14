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

	-- [설계] 프로필 이미지는 이 테이블에 컬럼으로 두지 않는다 — 유저당 여러
	-- 장을 저장하고 그중 하나를 "대표"로 고르는 구조라 user_profile_images
	-- 테이블(status=1인 행이 대표)이 유일한 출처다. 조회는
	-- "SELECT ... FROM user_profile_images WHERE user_public_id=? AND status=1"
	-- 로 한다(AccountDBHandler.cpp 재접속 경로 참고) — 비정규화 캐시 컬럼을
	-- 따로 안 둬서 값 불일치 걱정이 없다.

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
-- user_profile_images : 유저당 여러 장의 프로필 이미지를 저장하는 갤러리.
--   - 한 유저가 여러 이미지를 업로드/등록해두고 그중 하나를 "대표"로
--     고를 수 있다(카카오톡 프로필 사진 히스토리와 비슷한 개념).
--   - image_ref: "local:{상대경로}"(서버가 로컬 디스크에 저장한 파일,
--     IImageStorage/CLocalFileImageStorage 참고) 또는 실제 URL(외부
--     호스팅/CDN). 어느 쪽이든 클라이언트는 접두사로 구분해서 fetch
--     방식을 고른다(local: → 청크 다운로드 프로토콜, http(s): → HttpClient).
--   - [설계] 대표 이미지는 별도 캐시 컬럼 없이 이 테이블의 status(0/1)
--     컬럼만으로 판단한다 — 조회는 항상
--     "WHERE user_public_id=? AND status=1"로 한다. 대표를 바꿀 때는
--     기존 status=1 행을 먼저 0으로 내린 뒤 새 행(또는 기존 행)을 1로
--     올린다(SetProfileImageUrlDBHandler.cpp/SelectProfileImageDBHandler.cpp) —
--     두 UPDATE/INSERT가 하나의 명시적 트랜잭션으로 묶여있진 않아 그
--     사이에 서버가 죽으면 "대표가 하나도 없는" 상태로 남을 수 있다(실용적
--     타협 — 사용자가 다시 선택하면 바로 복구됨).
--   - [설계] FK를 users.uid가 아니라 users.public_id로 건다 — 이 프로젝트는
--     DB 계층 밖에서 계정을 항상 public_id(16진 32자)로 식별하므로(세션도
--     uid를 아예 모름), uid로 걸면 매번 "public_id -> uid 변환" 조회가
--     추가로 필요해진다. public_id는 UNIQUE 제약이 걸려 있어 PK가 아니어도
--     FK 대상이 될 수 있다.
-- ---------------------------------------------------------------------------
CREATE TABLE user_profile_images
(
	image_id     BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY COMMENT '이미지 고유 ID',
	user_public_id CHAR(32)     NOT NULL COMMENT 'users.public_id 참조 — 이 프로젝트 전체가 계정 식별에 쓰는 값과 동일',
	image_ref    VARCHAR(500) NOT NULL COMMENT '"local:{경로}" 또는 실제 URL',
	file_size    INT UNSIGNED NOT NULL DEFAULT 0 COMMENT '원본 파일 크기(바이트). local: 참조일 때만 의미 있고, 외부 URL이면 0',
	status       TINYINT   NOT NULL DEFAULT 0 COMMENT '0=일반, 1=대표(활성) 프로필 이미지',
	created_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '업로드/등록 시각',

	INDEX idx_user_profile_images_public_id (user_public_id),
	INDEX idx_user_profile_images_public_id_status (user_public_id, status),
	CONSTRAINT fk_user_profile_images_user
		FOREIGN KEY (user_public_id) REFERENCES users (public_id)
		ON DELETE CASCADE
)
ENGINE = InnoDB
DEFAULT CHARSET = utf8mb4
COLLATE = utf8mb4_unicode_ci
COMMENT = '유저당 여러 장 저장 가능한 프로필 이미지 갤러리';

-- ---------------------------------------------------------------------------
-- (선택) 애플리케이션 접속 계정 생성 — demo(ChatServer.cpp)의 CDBNode 설정과
-- 맞추려면 아래 계정 정보를 그대로 쓰거나, 실제 운영 값으로 바꿔서 실행하세요.
-- 이미 계정이 있다면 이 블록은 건너뛰어도 됩니다. DB 관리자 권한 필요.
-- ---------------------------------------------------------------------------
-- CREATE USER IF NOT EXISTS 'chat_user'@'%' IDENTIFIED BY 'chat_password';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.users TO 'chat_user'@'%';
-- FLUSH PRIVILEGES;

-- ---------------------------------------------------------------------------
-- (마이그레이션) 이미 users 테이블이 존재하는 기존 DB라면:
--   1) users.profile_image_url 컬럼을 예전에 추가했었다면 이제 필요 없으니
--      제거하세요(대표 이미지는 user_profile_images.status로만 관리).
--        ALTER TABLE users DROP COLUMN profile_image_url;
--   2) user_profile_images 테이블이 없다면 위 CREATE TABLE 블록을 그대로
--      실행하세요.
--   3) 신규 계정 생성 시 GRANT 대상에 이 테이블도 추가해야 합니다:
--        GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.user_profile_images TO 'chat_user'@'%';
-- ---------------------------------------------------------------------------