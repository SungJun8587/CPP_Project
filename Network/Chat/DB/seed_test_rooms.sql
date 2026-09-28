-- ***************************************************************************
-- seed_test_rooms.sql : 방 목록 페이징/검색 테스트용 더미 데이터 생성 (MySQL 5.7+ / 8.x)
--
-- 결과
--   - 더미 방장 계정 200개 생성 (nickname = seed_000 ~ seed_199)
--   - 방 2000개 생성 (방 이름은 "형용사 명사 번호"를 무작위로 조합,
--     생성 시각은 최근 90일 안에서 무작위)
--   - 방은 더미 방장 200명에게 10개씩 고르게 배정됨
--
-- 주의
--   - 더미 계정의 token_hash는 무작위 값이라 이 계정으로는 로그인할 수 없다.
--   - 이 스크립트는 DB에 직접 INSERT하므로 서버의 "방 개수 제한(MaxRoomsPerOwner)"
--     같은 애플리케이션 규칙은 적용되지 않는다.
--   - 서버는 시작할 때 rooms를 메모리(방 레지스트리)로 읽어온다. 방 "목록/검색/
--     페이징"은 DB를 직접 조회하므로 바로 보이지만, 이 방들에 "입장"하려면
--     스크립트 실행 후 서버를 재시작해야 한다(재시작 전에는 존재하지 않는
--     방으로 응답한다).
--   - 다시 실행하면 방이 2000개 더 추가된다. 지우려면 맨 아래 정리 구문을 쓴다.
-- ***************************************************************************

USE chat;

-- ---------------------------------------------------------------------------
-- 1. 번호표(0 ~ 1999). 재귀 CTE(8.0 전용)를 쓰지 않고 자리수 조합으로 만들어
--    5.7에서도 동작한다.
-- ---------------------------------------------------------------------------
DROP TEMPORARY TABLE IF EXISTS tmp_seed_numbers;
CREATE TEMPORARY TABLE tmp_seed_numbers
(
	n INT UNSIGNED NOT NULL PRIMARY KEY
);

INSERT INTO tmp_seed_numbers (n)
SELECT a.d + b.d * 10 + c.d * 100 + e.d * 1000
FROM (SELECT 0 AS d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) a
CROSS JOIN (SELECT 0 AS d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) b
CROSS JOIN (SELECT 0 AS d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) c
CROSS JOIN (SELECT 0 AS d UNION ALL SELECT 1 UNION ALL SELECT 2 UNION ALL SELECT 3 UNION ALL SELECT 4
      UNION ALL SELECT 5 UNION ALL SELECT 6 UNION ALL SELECT 7 UNION ALL SELECT 8 UNION ALL SELECT 9) e
WHERE a.d + b.d * 10 + c.d * 100 + e.d * 1000 < 2000;

-- ---------------------------------------------------------------------------
-- 2. 더미 방장 계정 200개. 이미 있으면(nickname UNIQUE) 건너뛴다.
-- ---------------------------------------------------------------------------
INSERT IGNORE INTO users (public_id, nickname, token_hash)
SELECT LOWER(HEX(RANDOM_BYTES(16))),
       CONCAT('seed_', LPAD(n, 3, '0')),
       SHA2(RANDOM_BYTES(32), 256)
FROM tmp_seed_numbers
WHERE n < 200;

-- ---------------------------------------------------------------------------
-- 3. 방 2000개. 이름은 "형용사 명사 번호"의 무작위 조합, 방장은 n % 200으로
--    고르게 배정, 생성 시각은 최근 90일 안에서 무작위.
-- ---------------------------------------------------------------------------
INSERT INTO rooms (name, owner_public_id, created_at)
SELECT
	LEFT(CONCAT(
		ELT(1 + FLOOR(RAND() * 12),
			'즐거운', '조용한', '뜨거운', '신나는', '엉뚱한', '느긋한',
			'수상한', '심심한', '귀여운', '든든한', '졸린', '활기찬'),
		' ',
		ELT(1 + FLOOR(RAND() * 16),
			'수다방', '토론방', '고양이방', '게임방', '여행방', '맛집방', '음악방', '영화방',
			'독서방', '운동방', '코딩방', '사진방', '스터디방', '아지트', '쉼터', '놀이터'),
		' ',
		LPAD(FLOOR(1 + RAND() * 999), 3, '0')
	), 50) AS name,
	(SELECT u.public_id FROM users u
	 WHERE u.nickname = CONCAT('seed_', LPAD(t.n % 200, 3, '0'))) AS owner_public_id,
	NOW() - INTERVAL FLOOR(RAND() * 90 * 24 * 60 * 60) SECOND AS created_at
FROM tmp_seed_numbers t
WHERE t.n < 2000;

DROP TEMPORARY TABLE IF EXISTS tmp_seed_numbers;

-- ---------------------------------------------------------------------------
-- 4. 확인
-- ---------------------------------------------------------------------------
SELECT COUNT(*) AS total_rooms FROM rooms;

SELECT r.room_id, r.name, u.nickname AS owner, r.created_at
FROM rooms r JOIN users u ON r.owner_public_id = u.public_id
ORDER BY r.created_at DESC, r.room_id DESC
LIMIT 10;

-- ---------------------------------------------------------------------------
-- (선택) 검색 이스케이프 확인용 방 3개. 검색창에 "100%" 또는 "test_" 를 입력하면
-- 각각 이 방만 나와야 한다("%", "_"가 와일드카드가 아니라 글자로 검색되는지 확인).
-- ---------------------------------------------------------------------------
-- INSERT INTO rooms (name, owner_public_id)
-- SELECT v.name, (SELECT public_id FROM users WHERE nickname = 'seed_000')
-- FROM (SELECT '열정 100% 방' AS name UNION ALL SELECT 'test_room_underscore'
--       UNION ALL SELECT '느낌표! 방') v;

-- ---------------------------------------------------------------------------
-- (선택) "내 방" 페이징 확인용 — 내 계정이 방 35개에 입장한 상태로 만든다.
-- '내_PUBLIC_ID'를 본인 계정의 public_id(users.public_id, 32자)로 바꿔서 실행.
-- ---------------------------------------------------------------------------
-- INSERT IGNORE INTO room_members (room_id, user_public_id)
-- SELECT room_id, '내_PUBLIC_ID' FROM rooms ORDER BY room_id DESC LIMIT 35;

-- ---------------------------------------------------------------------------
-- (정리) 더미 데이터 전부 삭제. 더미 계정을 지우면 FK(ON DELETE CASCADE)로 그
-- 계정이 만든 방과 입장 기록도 함께 지워진다.
-- ---------------------------------------------------------------------------
-- DELETE FROM users WHERE nickname LIKE 'seed\_%';