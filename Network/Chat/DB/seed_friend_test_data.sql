-- ***************************************************************************
-- seed_friend_test_data.sql : 친구 기능 테스트용 예시 데이터 (MySQL)
--
-- 사전 조건: create_chat_db.sql, create_friend_db.sql 실행 완료,
--            테스트에 쓸 "내 계정"이 클라이언트로 한 번 이상 가입돼 있을 것.
--
-- 사용법: 아래 [설정]의 닉네임만 바꿔서 통째로 실행. 여러 번 실행해도 안전(중복 무시).
--         되돌리기는 cleanup_friend_test_data.sql.
--
-- 만들어지는 더미 계정은 토큰 원문을 알 수 없어 로그인할 수 없습니다(목록에만 보임).
-- 온라인/오프라인 알림은 더미로 만들 수 없으니, 실제 클라이언트 두 개로 따로 확인하세요.
-- ***************************************************************************

USE chat_db;
SET NAMES utf8mb4;

-- ===== [설정] ==============================================================
-- 내 계정 닉네임(이미 가입된 계정)
SET @me = (SELECT public_id FROM users WHERE nickname = '내닉네임');

-- 1로 바꾸면 친구 상한(3000명) 테스트용으로 lim_001~lim_2992 를 추가 친구로 넣어
-- 기존 친구 8명과 합쳐 정확히 3000명으로 맞춘다(그 상태에서 요청 수락/발송 시 FriendLimit 확인).
SET @fill_to_limit = 0;
-- ===========================================================================

SELECT IF(@me IS NULL, '!! 내 계정 닉네임을 찾지 못했습니다 — 위 [설정]의 닉네임을 확인하세요 !!', 'OK: 내 계정 확인됨') AS check_me;

-- ---------------------------------------------------------------------------
-- 1) 더미 계정 목록 (임시 테이블)
--    kind: friend(이미 친구) / request(나에게 온 요청) / sent(내가 보낸 요청)
--          / blocked(내가 차단) / blockme(나를 차단) / lim(친구 상한 테스트)
-- ---------------------------------------------------------------------------
DROP TEMPORARY TABLE IF EXISTS seed_people;
CREATE TEMPORARY TABLE seed_people
(
	nickname VARCHAR(16) NOT NULL,
	kind     VARCHAR(8)  NOT NULL,
	idx      INT         NOT NULL,
	PRIMARY KEY (nickname)
) DEFAULT CHARSET = utf8mb4;

INSERT INTO seed_people (nickname, kind, idx) VALUES
	('철수', 'friend', 1), ('영희', 'friend', 2), ('민수', 'friend', 3), ('지영', 'friend', 4),
	('현우', 'friend', 5), ('friend_ann', 'friend', 6), ('friend_bob', 'friend', 7), ('friend_cho', 'friend', 8),
	('sent_01', 'sent', 1), ('sent_02', 'sent', 2),
	('blocked_01', 'blocked', 1), ('blocked_02', 'blocked', 2),
	('blockme_01', 'blockme', 1);

-- 받은 요청 120건(페이지 크기 50 기준 3페이지: 50/50/20)
DROP TEMPORARY TABLE IF EXISTS seed_nums;
CREATE TEMPORARY TABLE seed_nums (n INT NOT NULL PRIMARY KEY);
INSERT INTO seed_nums (n)
SELECT a.d + b.d * 10 + c.d * 100 + e.d * 1000 + 1
FROM (SELECT 0 d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) a,
     (SELECT 0 d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) b,
     (SELECT 0 d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) c,
     (SELECT 0 d UNION ALL SELECT 1 UNION ALL SELECT 2) e;

INSERT INTO seed_people (nickname, kind, idx)
SELECT CONCAT('req_', LPAD(n, 3, '0')), 'request', n FROM seed_nums WHERE n <= 120;

-- 친구 상한 테스트(옵션): 기존 친구 8명 + 2992명 = 3000명
INSERT INTO seed_people (nickname, kind, idx)
SELECT CONCAT('lim_', LPAD(n, 3, '0')), 'lim', n FROM seed_nums WHERE @fill_to_limit = 1 AND n <= 2992;

-- ---------------------------------------------------------------------------
-- 2) users — public_id는 닉네임으로부터 정해지는 고정 값(MD5, 소문자 16진 32자),
--    token_hash는 "알 수 없는 토큰"의 해시라 로그인 불가. 이미 있으면 건너뜀.
-- ---------------------------------------------------------------------------
INSERT IGNORE INTO users (public_id, nickname, token_hash)
SELECT MD5(CONCAT('friend-seed:', nickname)), nickname, SHA2(CONCAT('friend-seed-nologin:', nickname), 256)
FROM seed_people;

-- ---------------------------------------------------------------------------
-- 3) 프로필 이미지(대표) — 친구 전원 + 요청자 앞 10명. 상대 경로는 서버가
--    파일 서버 주소를 붙여 내려준다(실제 파일이 없으면 클라이언트에서 기본 이미지로 보임).
--    친구 1명은 외부 URL 형태로 넣어 두 형태를 모두 확인할 수 있게 함.
-- ---------------------------------------------------------------------------
INSERT INTO user_profile_images (user_public_id, image_ref, file_size, status)
SELECT u.public_id,
       IF(p.kind = 'friend' AND p.idx = 8, 'https://example.com/seed/avatar_friend_08.png',
          CONCAT('/images/seed/', p.nickname, '.png')),
       0, 1
FROM seed_people p
JOIN users u ON u.public_id = MD5(CONCAT('friend-seed:', p.nickname))
WHERE (p.kind = 'friend' OR (p.kind = 'request' AND p.idx <= 10))
  AND NOT EXISTS (SELECT 1 FROM user_profile_images i WHERE i.user_public_id = u.public_id);

-- ---------------------------------------------------------------------------
-- 4) 관계 데이터
-- ---------------------------------------------------------------------------
-- 4-1) 이미 친구 (user_a < user_b 로 정규화)
INSERT IGNORE INTO friendships (user_a, user_b, created_at)
SELECT LEAST(@me, MD5(CONCAT('friend-seed:', p.nickname))),
       GREATEST(@me, MD5(CONCAT('friend-seed:', p.nickname))),
       NOW() - INTERVAL p.idx DAY
FROM seed_people p
WHERE p.kind IN ('friend', 'lim');

-- 4-2) 나에게 온 요청 (오래된 순서 = 번호 순서)
INSERT IGNORE INTO friend_requests (from_id, to_id, created_at)
SELECT MD5(CONCAT('friend-seed:', p.nickname)), @me, NOW() - INTERVAL (200 - p.idx) MINUTE
FROM seed_people p
WHERE p.kind = 'request';

-- 4-3) 내가 보낸 요청 (상대가 수락 대기 중 — 같은 닉네임으로 다시 요청하면 AlreadyRequested)
INSERT IGNORE INTO friend_requests (from_id, to_id, created_at)
SELECT @me, MD5(CONCAT('friend-seed:', p.nickname)), NOW() - INTERVAL p.idx HOUR
FROM seed_people p
WHERE p.kind = 'sent';

-- 4-4) 내가 차단한 사람 (요청 시 BlockedByMe, 차단 목록에 표시)
INSERT IGNORE INTO user_blocks (blocker_id, blocked_id, created_at)
SELECT @me, MD5(CONCAT('friend-seed:', p.nickname)), NOW() - INTERVAL p.idx DAY
FROM seed_people p
WHERE p.kind = 'blocked';

-- 4-5) 나를 차단한 사람 (내가 요청하면 성공 응답이지만 저장/알림 없음)
INSERT IGNORE INTO user_blocks (blocker_id, blocked_id, created_at)
SELECT MD5(CONCAT('friend-seed:', p.nickname)), @me, NOW()
FROM seed_people p
WHERE p.kind = 'blockme';

-- ---------------------------------------------------------------------------
-- 5) 결과 확인
-- ---------------------------------------------------------------------------
SELECT 'friends'          AS item, COUNT(*) AS cnt FROM friendships    WHERE user_a = @me OR user_b = @me
UNION ALL SELECT 'pending received', COUNT(*) FROM friend_requests WHERE to_id = @me
UNION ALL SELECT 'pending sent',     COUNT(*) FROM friend_requests WHERE from_id = @me
UNION ALL SELECT 'blocked by me',    COUNT(*) FROM user_blocks     WHERE blocker_id = @me
UNION ALL SELECT 'blocked me',       COUNT(*) FROM user_blocks     WHERE blocked_id = @me;

DROP TEMPORARY TABLE IF EXISTS seed_nums;
DROP TEMPORARY TABLE IF EXISTS seed_people;
