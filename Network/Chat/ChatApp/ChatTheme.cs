
//***************************************************************************
// ChatTheme.cs : 앱 전체 색상 팔레트(스킨) 정의.
//
// [설계] ChatClientForm과 ProfileImageGalleryPanel(갤러리 탭)이 공통으로
// 참조하는 단일 진실 공급원 — ChatTheme.Current를 바꾸고 관련 컨트롤을
// 다시 그리면(Invalidate) 앱 전체의 색이 한 번에 바뀐다. 원래
// ChatClientForm.cs 안에 있었는데, 두 파일이 같이 참조하는 공용 정의라
// 별도 파일로 분리했다.
//
// [수정] 채팅 리스트박스 배경(ChatBackground)은 스킨과 무관하게 항상 같은
// 색으로 고정한다 — 스킨이 바뀌는 건 "내가 보낸 메시지" 말풍선(MyBubble)
// 색상뿐이다. 예전엔 ChatBackground도 스킨별로 달랐는데, 그러면 카드/상대
// 말풍선은 항상 흰색인데 리스트박스만 스킨 따라 어두워지는 등 일관성이
// 없었다 — 리스트박스 배경이 고정이라, 닉네임/시간 라벨도 이제 카드
// 라벨과 같은 TextSecondary/TextMuted를 그대로 쓰면 된다(별도
// ChatNameText/ChatTimeText 분리가 더 이상 필요 없음).
//***************************************************************************

using System.Drawing;

namespace ChatApp
{
    //***************************************************************************
    // @brief 스킨(테마) 하나를 이루는 색상 팔레트. ChatClientForm과
    //        ProfileImageGalleryPanel(갤러리 탭)이 공통으로 참조하는
    //        단일 진실 공급원 — ChatTheme.Current를 바꾸고 관련 컨트롤을
    //        다시 그리면(Invalidate) 앱 전체의 색이 한 번에 바뀐다.
    //***************************************************************************
    public class ChatTheme
    {
        //***************************************************************************
        // @brief 채팅 리스트박스 배경 — 스킨과 무관하게 항상 이 값(흰색)을
        //        쓴다. 카드/상대 말풍선 배경도 항상 흰색이라 톤을 맞췄다.
        //***************************************************************************
        public static readonly Color FixedChatBackground = Color.White;

        public string Name;
        public Color Accent;
        public Color Success;
        public Color Danger;
        public Color PageBack;
        public Color Border;
        public Color TextSecondary;
        public Color TextMuted;
        public Color TextTime;
        public Color HeaderBack;
        public Color MyBubble;
        public Color MyText;
        public Color OtherText;

        public static readonly ChatTheme Blue = new ChatTheme
        {
            Name = "블루 (기본)",
            Accent = Color.FromArgb(61, 123, 247),              // 주요 버튼/포인트
            Success = Color.FromArgb(47, 184, 112),             // 성공/확인
            Danger = Color.FromArgb(214, 69, 69),               // 경고/삭제
            PageBack = Color.FromArgb(247, 248, 250),           // 전체 배경
            HeaderBack = Color.White,                           // 상단 헤더 배경
            Border = Color.FromArgb(226, 228, 232),             // 테두리
            TextSecondary = Color.FromArgb(138, 143, 152),      // 부텍스트
            TextMuted = Color.FromArgb(176, 180, 186),          // 비활성 텍스트
            TextTime = Color.Black,                             // 시간 텍스트
            MyBubble = Color.FromArgb(61, 123, 247),            // 내 말풍선
            MyText = Color.White,                               // 내 말풍선 텍스트
            OtherText = Color.FromArgb(26, 29, 33),             // 상대방 텍스트
        };

        public static readonly ChatTheme Purple = new ChatTheme
        {
            Name = "퍼플 (보라)",
            Accent = Color.FromArgb(151, 111, 255),
            Success = Color.FromArgb(72, 199, 142),
            Danger = Color.FromArgb(235, 100, 100),
            PageBack = Color.FromArgb(30, 32, 36),
            HeaderBack = Color.FromArgb(24, 26, 29),
            Border = Color.FromArgb(210, 213, 217),
            TextSecondary = Color.FromArgb(100, 104, 110),
            TextMuted = Color.FromArgb(150, 154, 160),
            TextTime = Color.Black,
            MyBubble = Color.FromArgb(151, 111, 255),
            MyText = Color.White,
            OtherText = Color.FromArgb(26, 29, 33),
        };

        public static readonly ChatTheme Pink = new ChatTheme
        {
            Name = "핑크",
            Accent = Color.FromArgb(240, 98, 146),
            Success = Color.FromArgb(47, 184, 112),
            Danger = Color.FromArgb(214, 69, 69),
            PageBack = Color.FromArgb(255, 246, 249),
            HeaderBack = Color.White,
            Border = Color.FromArgb(245, 214, 226),
            TextSecondary = Color.FromArgb(150, 120, 130),
            TextMuted = Color.FromArgb(190, 160, 170),
            TextTime = Color.Black,
            MyBubble = Color.FromArgb(240, 98, 146),
            MyText = Color.White,
            OtherText = Color.FromArgb(60, 40, 46),
        };

        public static readonly ChatTheme Green = new ChatTheme
        {
            Name = "그린",
            Accent = Color.FromArgb(46, 160, 90),
            Success = Color.FromArgb(46, 160, 90),
            Danger = Color.FromArgb(214, 69, 69),
            PageBack = Color.FromArgb(246, 251, 247),
            HeaderBack = Color.White,
            Border = Color.FromArgb(212, 232, 216),
            TextSecondary = Color.FromArgb(110, 130, 115),
            TextMuted = Color.FromArgb(160, 180, 165),
            TextTime = Color.Black,
            MyBubble = Color.FromArgb(46, 160, 90),
            MyText = Color.White,
            OtherText = Color.FromArgb(26, 29, 33),
        };

        public static readonly ChatTheme Black = new ChatTheme
        {
            Name = "블랙",
            Accent = Color.FromArgb(45, 45, 48),
            Success = Color.FromArgb(47, 184, 112),
            Danger = Color.FromArgb(214, 69, 69),
            PageBack = Color.FromArgb(18, 18, 18),
            HeaderBack = Color.FromArgb(12, 12, 12),
            Border = Color.FromArgb(210, 210, 212),
            TextSecondary = Color.FromArgb(110, 110, 114),
            TextMuted = Color.FromArgb(160, 160, 164),
            TextTime = Color.Black,
            MyBubble = Color.FromArgb(35, 35, 38),
            MyText = Color.White,
            OtherText = Color.FromArgb(26, 29, 33),
        };

        //***************************************************************************
        // @brief 앱 전체가 지금 쓰고 있는 테마. 기본값은 Blue — ChatClientForm.
        //        ApplyTheme()가 스킨 선택 시 이 값을 바꾼다.
        // @details [수정] 이 필드는 반드시 Blue/Purple/Pink/Green/Black 프리셋들보다
        //          "뒤에" 선언돼야 한다 — C#의 정적 필드 초기화는 선언
        //          순서(위에서 아래로) 그대로 실행되므로, Current = Blue를
        //          Blue보다 먼저 선언하면 그 시점엔 Blue가 아직 null이라
        //          Current도 null로 초기화돼버린다(실제로 이 버그로
        //          NullReferenceException이 났었다).
        //***************************************************************************
        public static ChatTheme Current = Blue;

        public static readonly ChatTheme[] All = { Blue, Purple, Pink, Green, Black };
    }
}