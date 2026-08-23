-- MSSQL 테이블 생성
CREATE TABLE [dbo].[Producer](
	[No] [int] IDENTITY(1,1) NOT NULL,
	[Name1] [nvarchar](50) NULL,
	[Name2] [nvarchar](50) NOT NULL,
	[Flag] [tinyint] NOT NULL,
	[Age] [int] NULL,
 CONSTRAINT [pk_Producer_No] PRIMARY KEY CLUSTERED 
(
	[No] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
) ON [PRIMARY]
GO

CREATE TABLE [dbo].[Consumer](
	[No] [int] IDENTITY(1,1) NOT NULL,
	[Name1] [nvarchar](50) NULL,
	[Name2] [nvarchar](50) NOT NULL,
	[Flag] [tinyint] NOT NULL,
	[Age] [int] NULL,
 CONSTRAINT [pk_Consumer_No] PRIMARY KEY CLUSTERED 
(
	[No] ASC
)WITH (PAD_INDEX = OFF, STATISTICS_NORECOMPUTE = OFF, IGNORE_DUP_KEY = OFF, ALLOW_ROW_LOCKS = ON, ALLOW_PAGE_LOCKS = ON, OPTIMIZE_FOR_SEQUENTIAL_KEY = OFF) ON [PRIMARY]
) ON [PRIMARY]
GO


-- 100만 건의 대량 데이터
-- 1. 재귀 제한 해제 (기본값 초과 방지)
SET NOCOUNT ON;

-- 2. 100만 건 일괄 삽입
WITH 
    L0 AS (SELECT 1 AS c UNION ALL SELECT 1), -- 2^1 = 2
    L1 AS (SELECT 1 AS c FROM L0 AS A, L0 AS B), -- 2^2 = 4
    L2 AS (SELECT 1 AS c FROM L1 AS A, L1 AS B), -- 2^4 = 16
    L3 AS (SELECT 1 AS c FROM L2 AS A, L2 AS B), -- 2^8 = 256
    L4 AS (SELECT 1 AS c FROM L3 AS A, L3 AS B), -- 2^16 = 65,536
    L5 AS (SELECT 1 AS c FROM L4 AS A, L4 AS B), -- 2^32 = 4,294,967,296
    Numbers AS (
        SELECT ROW_NUMBER() OVER (ORDER BY (SELECT NULL)) AS n
        FROM L5
    )
INSERT INTO [dbo].[Producer] ([Name1], [Name2], [Flag], [Age])
SELECT TOP (1000000)
    CONCAT('FirstName_', n) AS [Name1],
    CONCAT('LastName_', n) AS [Name2],
    CAST(n % 2 AS tinyint) AS [Flag],              -- Flag (0 또는 1)
    20 + (n % 60) AS [Age]                         -- 20세~79세 사이 나이
FROM Numbers;


-- MYSQL 테이블 생성
CREATE TABLE `Producer` (
   `No` int NOT NULL AUTO_INCREMENT,
   `Name1` varchar(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT NULL,
   `Name2` varchar(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
   `Flag` bit(1) NOT NULL,
   `Age` int DEFAULT NULL,
   PRIMARY KEY (`No`)
 ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE `Consumer` (
   `No` int NOT NULL AUTO_INCREMENT,
   `Name1` varchar(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci DEFAULT NULL,
   `Name2` varchar(50) CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci NOT NULL,
   `Flag` bit(1) NOT NULL,
   `Age` int DEFAULT NULL,
   PRIMARY KEY (`No`)
 ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;


-- 100만 건의 대량 데이터
-- 1. 자동 커밋 끄기
SET autocommit = 0;

-- 2. 재귀 깊이 설정 (100만 이상)
SET SESSION cte_max_recursion_depth = 1000000;

-- 3. 데이터 삽입 실행
INSERT INTO `Producer` (`Name1`, `Name2`, `Flag`, `Age`)
WITH RECURSIVE `cte` AS (
    SELECT 1 AS `n`
    UNION ALL
    SELECT `n` + 1 FROM `cte` WHERE `n` < 1000000
)
SELECT 
    CONCAT('First_', `n`),
    CONCAT('Last_', `n`),
    MOD(`n`, 2),
    20 + MOD(`n`, 50)
FROM `cte`;

-- 4. 커밋 및 자동 커밋 복원
COMMIT;
SET autocommit = 1;