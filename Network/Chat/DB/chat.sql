-- ***************************************************************************
-- chat.sql : 채팅 서버 회원 DB 스키마 생성 스크립트 (MySQL)
--
--   - uid(내부 전용, AUTO_INCREMENT)를 PRIMARY KEY로 사용
--   - public_id(16바이트 무작위값을 16진 소문자 32자로 인코딩해 저장)를 외부 노출용 안정 식별자로 사용
--     (닉네임 변경과 무관하게 고정 — Redis 키/클라이언트 로컬 토큰 파일이
--     이 값을 기준으로 함)
--   - nickname은 이제 PK가 아니라 UNIQUE 제약만 걸린 표시용 컬럼
--     (INSERT/UPDATE 실패 시 중복 판별 근거는 이 UNIQUE 제약)
--   - token_hash는 Crypto::CCryptoUtil::HashSHA256()의 출력(16진 소문자, 64자)을 그대로 저장
--   - 토큰 원문은 DB에 저장하지 않음 — 해시만 저장(CryptoUtil.h/AccountDBHandler.cpp 설계 참고)
-- ***************************************************************************

CREATE DATABASE IF NOT EXISTS chat
	CHARACTER SET utf8mb4
	COLLATE utf8mb4_unicode_ci;

USE chat;

-- ---------------------------------------------------------------------------
-- users : uid = 내부 전용 순번 PK, public_id = 외부 노출용 안정 식별자,
--         nickname = 표시용(변경 가능), token_hash = 재접속 인증용 회전 토큰의 해시
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS users
(
	uid          BIGINT UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '내부 전용 순번 PK. 외부에 노출 금지',
	public_id    CHAR(32)     NOT NULL COMMENT '외부 노출용 안정 식별자(진짜 난수 16바이트를 16진 소문자 32자로 인코딩). 닉네임 변경과 무관하게 고정. token_hash와 동일한 이유(ODBC 바이너리 파라미터 바인딩의 드라이버별 불확실성 회피)로 BINARY(16) 대신 문자열로 저장',
	nickname     VARCHAR(16)  NOT NULL COMMENT '닉네임(표시용, 변경 가능). 영문/숫자/밑줄/한글, 1~16글자만 허용(NicknameValidation.h)',
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
	CONSTRAINT fk_user_profile_images_user FOREIGN KEY (user_public_id) REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB
DEFAULT CHARSET = utf8mb4
COLLATE = utf8mb4_unicode_ci
COMMENT = '유저당 여러 장 저장 가능한 프로필 이미지 갤러리';

-- ---------------------------------------------------------------------------
-- rooms : 동적으로 생성/삭제되는 이름 있는 채팅방. [신규]
--   - room_id는 이 테이블의 AUTO_INCREMENT 값을 그대로 프로토콜의 int32
--     roomId로 쓴다(1부터 시작 — MySQL AUTO_INCREMENT 기본 동작). 로비를
--     가리키는 예약값 kLobbyRoomId(0)과는 절대 충돌하지 않는다.
--   - owner_public_id: 방장의 계정. 방장이 방을 나가면(로비 이동/접속
--     종료) 서버가 남은 멤버 중 가장 오래 있었던 사람에게 자동으로
--     이양한다(TransferRoomOwnerDBHandler.cpp) — 그 방에 아무도 안
--     남았으면 방 자체를 삭제한다(방장 없는 빈 방을 남겨두지 않음).
--   - [설계] 방 생성 개수 제한(설정 파일의 MaxRoomsPerOwner)은 이 테이블에
--     제약으로 걸지 않는다 — CreateRoomDBHandler.cpp가 INSERT 전에
--     COUNT(*)로 직접 확인한다(설정값은 DB가 아니라 서버 설정 파일에
--     있으므로, DB 제약만으로는 표현할 수 없음).
-- ---------------------------------------------------------------------------
CREATE TABLE rooms
(
	room_id         INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY COMMENT '방 고유 번호(프로토콜의 roomId와 동일)',
	name            VARCHAR(50)  NOT NULL COMMENT '방 이름(표시용, 방장이 변경 가능)',
	owner_public_id CHAR(32)     NOT NULL COMMENT '방장 계정 — users.public_id 참조. 방장이 나가면 서버가 자동으로 다른 멤버에게 이양',
	image_ref       VARCHAR(500) NULL COMMENT '방 프로필 이미지 참조. NULL이면 기본 이미지(클라이언트가 방 이름 기반 아바타로 대체 표시)',
	created_at      DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '방 생성 시각',
 
	INDEX idx_rooms_owner_public_id (owner_public_id),
	CONSTRAINT fk_rooms_owner FOREIGN KEY (owner_public_id) REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB
DEFAULT CHARSET = utf8mb4
COLLATE = utf8mb4_unicode_ci
COMMENT = '동적으로 생성/삭제되는 이름 있는 채팅방';

-- ---------------------------------------------------------------------------
-- room_members : 사용자가 입장한 적이 있는 방의 기록("내가 참여한 방" 목록의 근거).
--   - 방에 입장할 때마다 서버가 기록한다(RecordRoomJoinDBHandler.cpp) — 처음
--     입장이면 행이 추가되고, 이미 있으면 last_entered_at만 갱신된다.
--   - "참여한 방" 목록은 last_entered_at 내림차순(최근 입장순)으로 보여준다.
--   - 방이 삭제되거나(rooms) 계정이 삭제되면(users) 그 행도 함께 삭제된다.
--   - 이 테이블은 "입장한 적이 있다"만 기록한다. 지금 그 방에 접속해 있는지는
--     서버 메모리(방 멤버 목록)가 유일한 출처다.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS room_members
(
	room_id          INT UNSIGNED NOT NULL COMMENT '방 번호 — rooms.room_id 참조',
	user_public_id   CHAR(32)     NOT NULL COMMENT '사용자 계정 — users.public_id 참조',
	first_joined_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '이 방에 처음 입장한 시각',
	last_entered_at  DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '이 방에 마지막으로 입장한 시각',
 
	PRIMARY KEY (room_id, user_public_id),
	INDEX idx_room_members_user (user_public_id, last_entered_at),
	CONSTRAINT fk_room_members_room FOREIGN KEY (room_id) REFERENCES rooms (room_id) ON DELETE CASCADE,
	CONSTRAINT fk_room_members_user FOREIGN KEY (user_public_id) REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB
DEFAULT CHARSET = utf8mb4
COLLATE = utf8mb4_unicode_ci
COMMENT = '사용자별 방 입장 기록';


-- ---------------------------------------------------------------------------
-- friendships : 수락된 친구 관계. 한 쌍당 정확히 한 행이며 user_a < user_b
--   (문자열 비교, 둘 다 소문자 16진 32자)로 정규화해서 저장한다 — 그래서
--   (A,B)와 (B,A)가 중복 저장될 수 없고, 조회는 양쪽 컬럼을 모두 본다.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS friendships
(
	user_a      CHAR(32) NOT NULL COMMENT '정규화된 쌍의 작은 쪽 public_id',
	user_b      CHAR(32) NOT NULL COMMENT '정규화된 쌍의 큰 쪽 public_id',
	created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '친구가 된 시각',
 
	PRIMARY KEY (user_a, user_b),
	INDEX idx_friendships_user_b (user_b),
	CONSTRAINT fk_friendships_a FOREIGN KEY (user_a) REFERENCES users (public_id) ON DELETE CASCADE,
	CONSTRAINT fk_friendships_b FOREIGN KEY (user_b) REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_unicode_ci
COMMENT = '친구 관계(정규화된 쌍당 1행)';
 
-- ---------------------------------------------------------------------------
-- friend_requests : 대기 중인 친구 요청 (from_id -> to_id). 수락/거절/취소 시 삭제.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS friend_requests
(
	from_id     CHAR(32) NOT NULL COMMENT '요청 보낸 계정 public_id',
	to_id       CHAR(32) NOT NULL COMMENT '요청 받은 계정 public_id',
	created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '요청 시각',
 
	PRIMARY KEY (from_id, to_id),
	INDEX idx_friend_requests_to (to_id, created_at),
	CONSTRAINT fk_friend_requests_from FOREIGN KEY (from_id) REFERENCES users (public_id) ON DELETE CASCADE,
	CONSTRAINT fk_friend_requests_to   FOREIGN KEY (to_id)   REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_unicode_ci
COMMENT = '대기 중인 친구 요청';
 
-- ---------------------------------------------------------------------------
-- user_blocks : 차단 (blocker_id가 blocked_id를 차단). 방향성이 있다.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS user_blocks
(
	blocker_id  CHAR(32) NOT NULL COMMENT '차단한 계정 public_id',
	blocked_id  CHAR(32) NOT NULL COMMENT '차단당한 계정 public_id',
	created_at  DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '차단 시각',
 
	PRIMARY KEY (blocker_id, blocked_id),
	INDEX idx_user_blocks_blocked (blocked_id),
	CONSTRAINT fk_user_blocks_blocker FOREIGN KEY (blocker_id) REFERENCES users (public_id) ON DELETE CASCADE,
	CONSTRAINT fk_user_blocks_blocked FOREIGN KEY (blocked_id) REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_unicode_ci
COMMENT = '차단 목록(방향성 있음)';

-- ---------------------------------------------------------------------------
-- conversations : 친구 기반 대화방(1:1 / 그룹). 공개 방(rooms)과는 별개다.
--   - conv_type 1=1:1, 2=그룹. 1:1은 두 계정당 하나만 존재해야 하므로 direct_key에
--     정규화된 "작은id:큰id"를 넣고 UNIQUE로 막는다(그룹은 NULL — UNIQUE는 NULL 중복 허용).
--   - last_msg_id : 이 방의 마지막 메시지 id(목록 정렬/미리보기용 비정규화 값).
--     메시지를 저장할 때마다 같은 핸들러가 갱신한다.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS conversations
(
	conv_id      BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY COMMENT '대화방 id(프로토콜의 convId)',
	conv_type    TINYINT      NOT NULL COMMENT '1=1:1, 2=그룹',
	name         VARCHAR(50)  NOT NULL DEFAULT '' COMMENT '그룹 이름(비어 있을 수 있음). 1:1은 항상 빈 문자열',
	direct_key   CHAR(65)     NULL COMMENT '1:1 전용 정규화 키 "작은public_id:큰public_id". 그룹은 NULL',
	last_msg_id  BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '마지막 메시지 id(0이면 메시지 없음)',
	created_at   DATETIME     NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '생성 시각',

	UNIQUE KEY uk_conversations_direct_key (direct_key)
)
ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_unicode_ci
COMMENT = '친구 기반 대화방(1:1/그룹)';

-- ---------------------------------------------------------------------------
-- conversation_members : 대화방 멤버십 + 읽음 위치.
--   - last_read_msg_id : 이 멤버가 읽은 마지막 msg_id. 안 읽은 수 = 그보다 큰 텍스트 메시지 수.
--   - visible_from_msg_id : 이 멤버에게 보이는 첫 메시지의 직전 id. 초대/재입장 시점의
--     last_msg_id로 채워서, 들어오기 전 대화를 볼 수 없게 한다.
--   - is_active : 1:1에서 "나가기"는 행을 지우지 않고 0으로 둔다(상대 정보를 목록에 유지하고,
--     상대가 보내거나 다시 열면 1로 되살린다). 그룹은 나가면 행을 삭제한다.
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS conversation_members
(
	conv_id              BIGINT UNSIGNED NOT NULL COMMENT 'conversations.conv_id',
	user_public_id       CHAR(32)        NOT NULL COMMENT 'users.public_id',
	last_read_msg_id     BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '읽은 마지막 msg_id',
	visible_from_msg_id  BIGINT UNSIGNED NOT NULL DEFAULT 0 COMMENT '이 id 이하 메시지는 이 멤버에게 보이지 않음',
	is_active            TINYINT         NOT NULL DEFAULT 1 COMMENT '0이면 목록에서 숨김(1:1 나가기)',
	joined_at            DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '참여 시각',

	PRIMARY KEY (conv_id, user_public_id),
	INDEX idx_conversation_members_user (user_public_id, is_active),
	CONSTRAINT fk_conv_members_conv FOREIGN KEY (conv_id) REFERENCES conversations (conv_id) ON DELETE CASCADE,
	CONSTRAINT fk_conv_members_user FOREIGN KEY (user_public_id) REFERENCES users (public_id) ON DELETE CASCADE
)
ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_unicode_ci
COMMENT = '대화방 멤버십 및 읽음 위치';

-- ---------------------------------------------------------------------------
-- conversation_messages : 대화 메시지. msg_id는 전역 AUTO_INCREMENT라 방 안에서도 단조 증가한다.
--   - kind 0=텍스트, 1=시스템 안내(sender_public_id NULL), 2=사진, 3=삭제됨(발신자가 삭제 — 소프트 삭제). 계정이 삭제돼도 메시지는 남도록
--     sender는 FK를 걸지 않고 값만 보관한다(표시는 users와 LEFT JOIN, 없으면 닉네임 빈 문자열).
-- ---------------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS conversation_messages
(
	msg_id            BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY COMMENT '메시지 id(전역 단조 증가)',
	conv_id           BIGINT UNSIGNED NOT NULL COMMENT 'conversations.conv_id',
	sender_public_id  CHAR(32)        NULL COMMENT '발신자 public_id. 시스템 메시지는 NULL',
	kind              TINYINT         NOT NULL DEFAULT 0 COMMENT '0=텍스트, 1=시스템, 2=사진(body=이미지 참조), 3=삭제됨(body 비움)',
	body              VARCHAR(1024)   NOT NULL COMMENT '본문(UTF-8). 시스템 메시지는 안내 문구, 사진은 이미지 참조',
	reply_to_msg_id   BIGINT UNSIGNED NULL COMMENT '답장 대상 msg_id (없으면 NULL)',
	created_at        DATETIME        NOT NULL DEFAULT CURRENT_TIMESTAMP COMMENT '전송 시각',
 
	INDEX idx_conversation_messages_conv (conv_id, msg_id),
	CONSTRAINT fk_conv_messages_conv FOREIGN KEY (conv_id) REFERENCES conversations (conv_id) ON DELETE CASCADE
)
ENGINE = InnoDB DEFAULT CHARSET = utf8mb4 COLLATE = utf8mb4_unicode_ci
COMMENT = '대화 메시지';
 
-- ---------------------------------------------------------------------------
-- (선택) 애플리케이션 접속 계정 생성 — demo(ChatServer.cpp)의 CDBNode 설정과
-- 맞추려면 아래 계정 정보를 그대로 쓰거나, 실제 운영 값으로 바꿔서 실행하세요.
-- 이미 계정이 있다면 이 블록은 건너뛰어도 됩니다. DB 관리자 권한 필요.
-- ---------------------------------------------------------------------------
-- CREATE USER IF NOT EXISTS 'chat_user'@'%' IDENTIFIED BY 'chat_password';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat. users TO 'chat_user'@'%';
-- FLUSH PRIVILEGES;
 
-- ---------------------------------------------------------------------------
-- (마이그레이션) 이미 users/user_profile_images 테이블이 있는 기존 DB에
-- rooms 기능만 추가하려면 위 CREATE TABLE rooms 블록만 그대로 실행하고,
-- 아래 GRANT도 추가하세요:
--   GRANT SELECT, INSERT, UPDATE, DELETE ON chat. rooms TO 'chat_user'@'%';
-- ---------------------------------------------------------------------------
 
-- 접속 계정에 권한이 필요하면(이미 chat. * 전체 권한이면 불필요):
--   GRANT SELECT, INSERT, UPDATE, DELETE ON chat. room_members TO 'chat_user'@'%';
 
 
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.friendships TO 'chat_user'@'%';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.friend_requests TO 'chat_user'@'%';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.user_blocks TO 'chat_user'@'%';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.conversations TO 'chat_user'@'%';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.conversation_members TO 'chat_user'@'%';
-- GRANT SELECT, INSERT, UPDATE, DELETE ON chat_db.conversation_messages TO 'chat_user'@'%';