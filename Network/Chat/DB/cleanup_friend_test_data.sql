-- ***************************************************************************
-- cleanup_friend_test_data.sql : seed_friend_test_data.sql 이 만든 더미 데이터 삭제
--
-- token_hash 가 SHA2('friend-seed-nologin:' + 닉네임) 인 계정만 지운다(= 시드가 만든
-- 계정). 실제 가입 계정은 건드리지 않는다. friendships / friend_requests /
-- user_blocks / user_profile_images 는 FK ON DELETE CASCADE 로 함께 삭제된다.
-- ***************************************************************************

USE chat_db;

DELETE FROM users
WHERE token_hash = SHA2(CONCAT('friend-seed-nologin:', nickname), 256)
  AND public_id  = MD5(CONCAT('friend-seed:', nickname));

SELECT ROW_COUNT() AS deleted_seed_users;
