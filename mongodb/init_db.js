/*
 * 古代中医经络穴位数字化与针刺疗效关联分析系统
 * MongoDB 初始化脚本
 * 用法: mongosh --file init_db.js
 */

const DB_NAME = "tcm_acupuncture";
const db = db.getSiblingDB(DB_NAME);

print("========================================");
print("中医经络数字化系统 - MongoDB初始化");
print("========================================");

// ================== 经络数据 ==================
const meridians = [
    {id:"LU", name:"手太阴肺经", pinyin:"Shoutaiyin Feijing", element:"金",
     path_points:[[260,180],[280,220],[300,270],[320,320],[340,370],[360,400],[375,430],[385,455],[395,470]],
     acupoint_ids:["LU1","LU5","LU7","LU9","LU11"]},
    {id:"LI", name:"手阳明大肠经", pinyin:"Shouyangming Dachangjing", element:"金",
     path_points:[[420,470],[430,455],[440,440],[455,410],[470,380],[485,350],[500,310],[520,260],[540,210]],
     acupoint_ids:["LI1","LI4","LI10","LI11","LI15"]},
    {id:"ST", name:"足阳明胃经", pinyin:"Zuyangming Weijing", element:"土",
     path_points:[[530,105],[525,135],[515,170],[505,210],[480,230],[445,245],[420,260],[410,340],[405,420],[400,520],[415,600],[420,650],[420,680]],
     acupoint_ids:["ST1","ST4","ST25","ST36","ST40","ST44"]},
    {id:"SP", name:"足太阴脾经", pinyin:"Zutaiyin Pijing", element:"土",
     path_points:[[330,685],[320,665],[310,640],[295,590],[285,550],[275,500],[265,450],[260,400],[255,340],[250,290],[245,260]],
     acupoint_ids:["SP1","SP3","SP6","SP9","SP10"]},
    {id:"HT", name:"手少阴心经", pinyin:"Shoushaoyin Xinjing", element:"火",
     path_points:[[230,200],[235,250],[240,290],[245,320],[250,370],[255,410],[260,450],[270,465],[275,475]],
     acupoint_ids:["HT1","HT3","HT7","HT9"]},
    {id:"SI", name:"手太阳小肠经", pinyin:"Shoutaiyang Xiaochangjing", element:"火",
     path_points:[[560,475],[570,460],[580,450],[578,425],[575,400],[575,340],[575,280],[575,220]],
     acupoint_ids:["SI1","SI3","SI6","SI9"]},
    {id:"BL", name:"足太阳膀胱经", pinyin:"Zutaiyang Pangguangjing", element:"水",
     path_points:[[490,100],[445,100],[400,175],[330,200],[330,240],[330,280],[330,320],[330,370],[330,430],[330,510],[330,580],[330,620],[340,670],[350,685]],
     acupoint_ids:["BL1","BL10","BL13","BL15","BL17","BL20","BL23","BL40","BL57","BL60","BL67"]},
    {id:"KI", name:"足少阴肾经", pinyin:"Zushaoyin Shenjing", element:"水",
     path_points:[[270,685],[255,675],[250,670],[240,660],[230,645],[220,625],[215,590],[210,550],[205,500],[205,440],[210,380],[215,320],[220,280]],
     acupoint_ids:["KI1","KI3","KI6","KI7"]},
    {id:"PC", name:"手厥阴心包经", pinyin:"Shoujueyin Xinbaojing", element:"火",
     path_points:[[290,200],[290,250],[290,320],[295,360],[300,390],[305,410],[310,440],[315,460]],
     acupoint_ids:["PC3","PC6","PC7","PC8"]},
    {id:"TE", name:"手少阳三焦经", pinyin:"Shoushaoyang Sanjiaojing", element:"火",
     path_points:[[555,440],[552,425],[550,410],[547,380],[545,350],[540,320],[545,280],[550,250],[555,215],[530,165],[515,120],[505,85]],
     acupoint_ids:["TE3","TE5","TE10","TE14","TE20"]},
    {id:"GB", name:"足少阳胆经", pinyin:"Zushaoyang Danjing", element:"木",
     path_points:[[555,105],[545,95],[540,80],[530,95],[505,125],[465,155],[430,175],[415,195],[405,210],[435,270],[455,350],[465,420],[470,500],[485,560],[495,620],[495,660],[490,670],[475,680]],
     acupoint_ids:["GB1","GB14","GB20","GB21","GB30","GB31","GB34","GB39","GB40","GB41"]},
    {id:"LR", name:"足厥阴肝经", pinyin:"Zujueyin Ganjing", element:"木",
     path_points:[[215,680],[205,670],[200,665],[195,650],[195,625],[200,580],[210,540],[220,500],[225,460],[230,400],[230,340],[230,280],[230,260]],
     acupoint_ids:["LR2","LR3","LR14"]},
    {id:"GV", name:"督脉", pinyin:"Dumai", element:"阳脉之海",
     path_points:[[350,685],[345,670],[340,640],[335,600],[335,520],[335,450],[335,380],[335,320],[340,260],[350,220],[370,205],[400,205],[400,175],[400,140],[400,90],[400,55],[405,120]],
     acupoint_ids:["GV14","GV16","GV20","GV24","GV26"]},
    {id:"CV", name:"任脉", pinyin:"Renmai", element:"阴脉之海",
     path_points:[[410,125],[405,140],[400,155],[400,175],[400,190],[400,215],[400,240],[400,260],[400,275],[400,295],[400,315],[400,340]],
     acupoint_ids:["CV3","CV4","CV6","CV8","CV12","CV17","CV22","CV24"]}
];

db.meridians.drop();
db.meridians.insertMany(meridians);
print("✓ 已插入 " + meridians.length + " 条经络数据");

// ================== 穴位数据 ==================
const acupoints = [
    {id:"LU1", name:"中府", pinyin:"Zhongfu", meridian_id:"LU", x:260, y:180, z:0, description:"肺募穴，治咳嗽气喘", indications:["咳嗽","气喘","胸痛"]},
    {id:"LU5", name:"尺泽", pinyin:"Chize", meridian_id:"LU", x:320, y:320, z:0, description:"合穴，治肺热", indications:["咳嗽","肘臂痛","潮热"]},
    {id:"LU7", name:"列缺", pinyin:"Lieque", meridian_id:"LU", x:360, y:400, z:0, description:"络穴八脉交会穴", indications:["头痛","项强","咳嗽"]},
    {id:"LU9", name:"太渊", pinyin:"Taiyuan", meridian_id:"LU", x:380, y:450, z:0, description:"输穴原穴脉会", indications:["咳嗽","无脉症","腕痛"]},
    {id:"LU11", name:"少商", pinyin:"Shaoshang", meridian_id:"LU", x:395, y:470, z:0, description:"井穴，治咽喉肿痛", indications:["咽喉肿痛","发热","昏迷"]},

    {id:"LI1", name:"商阳", pinyin:"Shangyang", meridian_id:"LI", x:420, y:470, z:0, description:"井穴，治齿痛", indications:["齿痛","咽喉肿痛","热病"]},
    {id:"LI4", name:"合谷", pinyin:"Hegu", meridian_id:"LI", x:440, y:440, z:0, description:"原穴，四总穴之一", indications:["头痛","面瘫","感冒"]},
    {id:"LI10", name:"手三里", pinyin:"Shousanli", meridian_id:"LI", x:480, y:360, z:0, description:"治上肢不遂", indications:["上肢痹痛","腹痛","腹泻"]},
    {id:"LI11", name:"曲池", pinyin:"Quchi", meridian_id:"LI", x:500, y:310, z:0, description:"合穴，治热病高血压", indications:["热病","高血压","湿疹"]},
    {id:"LI15", name:"肩髃", pinyin:"Jianyu", meridian_id:"LI", x:540, y:210, z:0, description:"治肩臂疼痛", indications:["肩臂挛痛","上肢不遂"]},

    {id:"ST1", name:"承泣", pinyin:"Chengqi", meridian_id:"ST", x:530, y:105, z:0, description:"治目疾", indications:["目赤肿痛","近视","口眼歪斜"]},
    {id:"ST4", name:"地仓", pinyin:"Dicang", meridian_id:"ST", x:525, y:135, z:0, description:"治口歪", indications:["口歪","流涎","眼睑瞤动"]},
    {id:"ST25", name:"天枢", pinyin:"Tianshu", meridian_id:"ST", x:400, y:260, z:0, description:"大肠募穴", indications:["腹痛","腹胀","便秘"]},
    {id:"ST36", name:"足三里", pinyin:"Zusanli", meridian_id:"ST", x:400, y:520, z:0, description:"合穴，保健要穴", indications:["胃痛","呕吐","虚劳"]},
    {id:"ST40", name:"丰隆", pinyin:"Fenglong", meridian_id:"ST", x:415, y:600, z:0, description:"络穴，化痰要穴", indications:["痰多","眩晕","头痛"]},
    {id:"ST44", name:"内庭", pinyin:"Neiting", meridian_id:"ST", x:420, y:680, z:0, description:"荥穴，治胃火牙痛", indications:["牙痛","咽喉肿痛","胃病"]},

    {id:"SP1", name:"隐白", pinyin:"Yinbai", meridian_id:"SP", x:330, y:685, z:0, description:"井穴，治出血证", indications:["月经过多","便血","癫狂"]},
    {id:"SP3", name:"太白", pinyin:"Taibai", meridian_id:"SP", x:315, y:650, z:0, description:"输穴原穴", indications:["胃痛","腹胀","泄泻"]},
    {id:"SP6", name:"三阴交", pinyin:"Sanyinjiao", meridian_id:"SP", x:295, y:580, z:0, description:"足三阴交会穴", indications:["月经不调","失眠","脾胃虚弱"]},
    {id:"SP9", name:"阴陵泉", pinyin:"Yinlingquan", meridian_id:"SP", x:275, y:500, z:0, description:"合穴，利水渗湿", indications:["水肿","腹胀","泄泻"]},
    {id:"SP10", name:"血海", pinyin:"Xuehai", meridian_id:"SP", x:260, y:420, z:0, description:"治血症皮肤病", indications:["月经不调","湿疹","丹毒"]},

    {id:"HT1", name:"极泉", pinyin:"Jiquan", meridian_id:"HT", x:230, y:200, z:0, description:"治心痛肩痛", indications:["心痛","咽干烦渴","肩臂痛"]},
    {id:"HT3", name:"少海", pinyin:"Shaohai", meridian_id:"HT", x:245, y:320, z:0, description:"合穴", indications:["心痛","肘臂挛痛","瘰疬"]},
    {id:"HT7", name:"神门", pinyin:"Shenmen", meridian_id:"HT", x:260, y:450, z:0, description:"输穴原穴，安神", indications:["失眠","健忘","心悸"]},
    {id:"HT9", name:"少冲", pinyin:"Shaochong", meridian_id:"HT", x:275, y:475, z:0, description:"井穴", indications:["心悸","心痛","昏迷"]},

    {id:"SI1", name:"少泽", pinyin:"Shaoze", meridian_id:"SI", x:560, y:475, z:0, description:"井穴，通乳", indications:["乳少","昏迷","热病"]},
    {id:"SI3", name:"后溪", pinyin:"Houxi", meridian_id:"SI", x:580, y:450, z:0, description:"输穴八脉交会穴", indications:["头项强痛","耳聋","疟疾"]},
    {id:"SI6", name:"养老", pinyin:"Yanglao", meridian_id:"SI", x:570, y:400, z:0, description:"郄穴，明目", indications:["目视不明","肩背肘痛"]},
    {id:"SI9", name:"肩贞", pinyin:"Jianzhen", meridian_id:"SI", x:575, y:220, z:0, description:"治肩痛", indications:["肩臂疼痛","瘰疬"]},

    {id:"BL1", name:"睛明", pinyin:"Jingming", meridian_id:"BL", x:490, y:100, z:0, description:"治目疾要穴", indications:["近视","目赤肿痛","夜盲"]},
    {id:"BL10", name:"天柱", pinyin:"Tianzhu", meridian_id:"BL", x:400, y:175, z:0, description:"治头痛项强", indications:["头痛","项强","鼻塞"]},
    {id:"BL13", name:"肺俞", pinyin:"Feishu", meridian_id:"BL", x:330, y:220, z:0, description:"肺背俞穴", indications:["咳嗽","气喘","骨蒸潮热"]},
    {id:"BL15", name:"心俞", pinyin:"Xinshu", meridian_id:"BL", x:330, y:250, z:0, description:"心背俞穴", indications:["心悸","失眠","健忘"]},
    {id:"BL17", name:"膈俞", pinyin:"Geshu", meridian_id:"BL", x:330, y:280, z:0, description:"血会", indications:["贫血","呕吐","呃逆"]},
    {id:"BL20", name:"脾俞", pinyin:"Pishu", meridian_id:"BL", x:330, y:320, z:0, description:"脾背俞穴", indications:["腹胀","泄泻","水肿"]},
    {id:"BL23", name:"肾俞", pinyin:"Shenshu", meridian_id:"BL", x:330, y:370, z:0, description:"肾背俞穴", indications:["腰痛","遗精","月经不调"]},
    {id:"BL40", name:"委中", pinyin:"Weizhong", meridian_id:"BL", x:330, y:510, z:0, description:"合穴，治腰痛", indications:["腰痛","下肢痿痹","腹痛"]},
    {id:"BL57", name:"承山", pinyin:"Chengshan", meridian_id:"BL", x:330, y:620, z:0, description:"治痔疮转筋", indications:["腰腿拘急","痔疮","便秘"]},
    {id:"BL60", name:"昆仑", pinyin:"Kunlun", meridian_id:"BL", x:340, y:670, z:0, description:"经穴", indications:["头痛","项强","腰痛"]},
    {id:"BL67", name:"至阴", pinyin:"Zhiyin", meridian_id:"BL", x:350, y:685, z:0, description:"井穴，转胎", indications:["胎位不正","难产","头痛"]},

    {id:"KI1", name:"涌泉", pinyin:"Yongquan", meridian_id:"KI", x:270, y:685, z:0, description:"井穴，急救", indications:["昏迷","中暑","失眠"]},
    {id:"KI3", name:"太溪", pinyin:"Taixi", meridian_id:"KI", x:250, y:670, z:0, description:"输穴原穴", indications:["肾虚","腰痛","月经不调"]},
    {id:"KI6", name:"照海", pinyin:"Zhaohai", meridian_id:"KI", x:235, y:660, z:0, description:"八脉交会穴", indications:["失眠","咽干","月经不调"]},
    {id:"KI7", name:"复溜", pinyin:"Fuliu", meridian_id:"KI", x:220, y:640, z:0, description:"经穴", indications:["水肿","盗汗","热病汗不出"]},

    {id:"PC3", name:"曲泽", pinyin:"Quze", meridian_id:"PC", x:290, y:320, z:0, description:"合穴", indications:["心痛","胃痛","呕血"]},
    {id:"PC6", name:"内关", pinyin:"Neiguan", meridian_id:"PC", x:305, y:410, z:0, description:"络穴八脉交会穴", indications:["心痛","呕吐","失眠"]},
    {id:"PC7", name:"大陵", pinyin:"Daling", meridian_id:"PC", x:310, y:440, z:0, description:"输穴原穴", indications:["心悸","胃痛","呕吐"]},
    {id:"PC8", name:"劳宫", pinyin:"Laogong", meridian_id:"PC", x:315, y:460, z:0, description:"荥穴", indications:["口疮","口臭","癫狂"]},

    {id:"TE3", name:"中渚", pinyin:"Zhongzhu", meridian_id:"TE", x:555, y:440, z:0, description:"输穴", indications:["头痛","耳鸣","肘臂痛"]},
    {id:"TE5", name:"外关", pinyin:"Waiguan", meridian_id:"TE", x:550, y:410, z:0, description:"络穴八脉交会穴", indications:["感冒","头痛","上肢痹痛"]},
    {id:"TE10", name:"天井", pinyin:"Tianjing", meridian_id:"TE", x:540, y:320, z:0, description:"合穴", indications:["偏头痛","瘰疬"]},
    {id:"TE14", name:"肩髎", pinyin:"Jianliao", meridian_id:"TE", x:555, y:215, z:0, description:"治肩臂痛", indications:["肩臂痛","上肢痿痹"]},
    {id:"TE20", name:"角孙", pinyin:"Jiaosun", meridian_id:"TE", x:505, y:85, z:0, description:"治偏头痛", indications:["偏头痛","耳部肿痛"]},

    {id:"GB1", name:"瞳子髎", pinyin:"Tongziliao", meridian_id:"GB", x:555, y:105, z:0, description:"治目疾", indications:["头痛","目赤肿痛"]},
    {id:"GB14", name:"阳白", pinyin:"Yangbai", meridian_id:"GB", x:540, y:80, z:0, description:"治面瘫", indications:["头痛","目痛","面瘫"]},
    {id:"GB20", name:"风池", pinyin:"Fengchi", meridian_id:"GB", x:430, y:175, z:0, description:"治感冒头痛", indications:["感冒","眩晕","颈项强痛"]},
    {id:"GB21", name:"肩井", pinyin:"Jianjing", meridian_id:"GB", x:400, y:210, z:0, description:"治肩痛难产", indications:["肩背痹痛","乳痈","难产"]},
    {id:"GB30", name:"环跳", pinyin:"Huantiao", meridian_id:"GB", x:460, y:430, z:0, description:"治腰腿痛", indications:["腰腿痛","下肢痿痹"]},
    {id:"GB31", name:"风市", pinyin:"Fengshi", meridian_id:"GB", x:470, y:500, z:0, description:"治下肢痹痛", indications:["下肢痿痹","遍身瘙痒"]},
    {id:"GB34", name:"阳陵泉", pinyin:"Yanglingquan", meridian_id:"GB", x:485, y:560, z:0, description:"合穴筋会", indications:["胁痛","口苦","下肢痿痹"]},
    {id:"GB39", name:"悬钟", pinyin:"Xuanzhong", meridian_id:"GB", x:495, y:640, z:0, description:"髓会", indications:["痴呆","中风","颈项强痛"]},
    {id:"GB40", name:"丘墟", pinyin:"Qiuxu", meridian_id:"GB", x:490, y:670, z:0, description:"原穴", indications:["胸胁痛","下肢痿痹"]},
    {id:"GB41", name:"足临泣", pinyin:"Zulinqi", meridian_id:"GB", x:475, y:680, z:0, description:"输穴八脉交会穴", indications:["偏头痛","目疾","乳痈"]},

    {id:"LR2", name:"行间", pinyin:"Xingjian", meridian_id:"LR", x:215, y:680, z:0, description:"荥穴，清肝火", indications:["头痛","眩晕","目赤"]},
    {id:"LR3", name:"太冲", pinyin:"Taichong", meridian_id:"LR", x:200, y:665, z:0, description:"输穴原穴", indications:["头痛","眩晕","月经不调"]},
    {id:"LR14", name:"期门", pinyin:"Qimen", meridian_id:"LR", x:230, y:260, z:0, description:"肝募穴", indications:["胸胁胀痛","乳痈","呕吐"]},

    {id:"GV14", name:"大椎", pinyin:"Dazhui", meridian_id:"GV", x:400, y:205, z:0, description:"诸阳之会", indications:["热病","感冒","项强"]},
    {id:"GV16", name:"风府", pinyin:"Fengfu", meridian_id:"GV", x:400, y:165, z:0, description:"治头痛项强", indications:["头痛","项强","中风"]},
    {id:"GV20", name:"百会", pinyin:"Baihui", meridian_id:"GV", x:400, y:55, z:0, description:"治头痛中风", indications:["头痛","眩晕","脱肛"]},
    {id:"GV24", name:"神庭", pinyin:"Shenting", meridian_id:"GV", x:400, y:90, z:0, description:"治失眠", indications:["失眠","头痛","惊悸"]},
    {id:"GV26", name:"人中", pinyin:"Renzhong", meridian_id:"GV", x:405, y:120, z:0, description:"急救要穴", indications:["昏迷","晕厥","癫狂"]},

    {id:"CV3", name:"中极", pinyin:"Zhongji", meridian_id:"CV", x:400, y:340, z:0, description:"膀胱募穴", indications:["遗尿","小便不利","月经不调"]},
    {id:"CV4", name:"关元", pinyin:"Guanyuan", meridian_id:"CV", x:400, y:315, z:0, description:"强壮保健穴", indications:["虚劳","阳痿","月经不调"]},
    {id:"CV6", name:"气海", pinyin:"Qihai", meridian_id:"CV", x:400, y:295, z:0, description:"补气要穴", indications:["虚脱","腹痛","泄泻"]},
    {id:"CV8", name:"神阙", pinyin:"Shenque", meridian_id:"CV", x:400, y:275, z:0, description:"治腹痛泄泻", indications:["腹痛","泄泻","虚脱"]},
    {id:"CV12", name:"中脘", pinyin:"Zhongwan", meridian_id:"CV", x:400, y:240, z:0, description:"胃募穴八会穴", indications:["胃痛","呕吐","癫狂"]},
    {id:"CV17", name:"膻中", pinyin:"Danzhong", meridian_id:"CV", x:400, y:190, z:0, description:"气会心包募", indications:["咳嗽","气喘","乳少"]},
    {id:"CV22", name:"天突", pinyin:"Tiantu", meridian_id:"CV", x:400, y:155, z:0, description:"治咳嗽哮喘", indications:["咳嗽","哮喘","暴喑"]},
    {id:"CV24", name:"承浆", pinyin:"Chengjiang", meridian_id:"CV", x:410, y:125, z:0, description:"治口歪", indications:["口歪","齿龈肿痛"]}
];

db.acupoints.drop();
db.acupoints.insertMany(acupoints);
print("✓ 已插入 " + acupoints.length + " 条穴位数据");

// ================== 志愿者数据（30名模拟） ==================
const volunteers = [];
const genders = ["男", "女"];
for (let i = 1; i <= 30; i++) {
    volunteers.push({
        volunteer_id: "V" + String(i).padStart(3, "0"),
        name: "志愿者" + i,
        gender: genders[i % 2],
        age: 20 + (i % 35),
        height: 155 + (i * 3) % 40,
        weight: 45 + (i * 5) % 40,
        constitution: ["平和质", "气虚质", "阳虚质", "阴虚质", "痰湿质"][i % 5],
        medical_history: [],
        registration_time: new Date()
    });
}
db.volunteers.drop();
db.volunteers.insertMany(volunteers);
print("✓ 已插入 " + volunteers.length + " 名志愿者数据");

// ================== 索引创建 ==================
print("\n正在创建索引...");

db.sensor_data.createIndex({ volunteer_id: 1, acupoint_id: 1, timestamp: -1 }, { name: "idx_sensor_query" });
db.sensor_data.createIndex({ timestamp: 1 }, { name: "idx_sensor_ts", expireAfterSeconds: 31536000 });
print("✓ sensor_data 索引创建完成");

db.alerts.createIndex({ timestamp: -1 }, { name: "idx_alerts_ts" });
db.alerts.createIndex({ acknowledged: 1, timestamp: -1 }, { name: "idx_alerts_ack" });
print("✓ alerts 索引创建完成");

db.efficacy_records.createIndex({ volunteer_id: 1, session_id: 1, timestamp: -1 }, { name: "idx_efficacy" });
db.predictions.createIndex({ session_id: 1, timestamp: -1 }, { name: "idx_pred" });
db.volunteers.createIndex({ volunteer_id: 1 }, { name: "idx_vol", unique: true });
db.acupoints.createIndex({ id: 1 }, { name: "idx_ap", unique: true });
db.meridians.createIndex({ id: 1 }, { name: "idx_mer", unique: true });

print("\n========================================");
print("MongoDB 初始化完成！");
print("数据库名: " + DB_NAME);
print("========================================");
