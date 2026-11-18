# Dataset การดื่มน้ำ v1: ที่มาของตัวเลข

`office_hydration_v1.csv` เป็น **ข้อมูลจำลอง** ไม่ใช่ข้อมูลที่วัดจากคนจริง
ค้นแล้วไม่พบ dataset สาธารณะที่มีเวลาการดื่ม + ปริมาณ + สภาพห้อง ของคนทำงานโต๊ะ
(ที่มีอยู่เป็นข้อมูล IMU ท่าทางการยกแก้ว เช่น OHM [S6] หรือ wristband [S7] ไม่มีปริมาณน้ำ)
จึงจำลองโดยตั้งค่าตามตัวเลขจากงานวิจัยเท่าที่หาได้ และแยกส่วนที่เป็นสมมติฐานให้เห็นชัด

สร้างใหม่: `python ai/make_dataset.py` (ค่าทั้งหมดอยู่บนสุดของไฟล์)

## ค่าที่มาจากงานวิจัย

| ค่าใน make_dataset.py | ค่า | ที่มา |
|---|---|---|
| `DAILY_DRINK_MEDIAN_ML` | 1,556 ml/วัน (น้ำเปล่า) | [S1] คนทำงานอินโดนีเซีย 246 คน บันทึก 7 วัน median น้ำเปล่า 1,556 ml/วัน, total fluid 1,882 (IQR 1,473–2,433), 53.7 % ดื่มไม่ถึงเกณฑ์ |
| `DAILY_DRINK_IQR` | 1,473–2,433 | [S1] ใช้กำหนดความกว้างของการกระจายระหว่างคน (lognormal) |
| `SWALLOW_ML_MEAN` | 16 ml | [S2] ขนาดจิบน้ำตามธรรมชาติเฉลี่ย ~16 ml |
| `need_ml` ใน train.py | 100 ml/ชม. | [S3] EFSA: น้ำรวม 2.0 L (หญิง) / 2.5 L (ชาย) ต่อวัน, 20–30 % มาจากอาหาร → ดื่ม 1.5–2.0 L ใน ~16 ชม. ≈ 95–125 ml/ชม. |
| `ROOM_T_MEAN/SD` | 26.35 ± 0.96 °C (23.9–28.8) | [S4] วัดในห้องแอร์ (สำนักงาน/ห้องเรียน) ภาคใต้ของไทย 300 คน; อุณหภูมิที่รู้สึกสบาย ~26.3 °C |
| `ROOM_RH_MEAN/SD` | 57.2 ± 5.6 % (41–71) | [S4] |
| `TEMP_EFFECT_PER_C` | +5.69 g/วัน ต่อ 1 °C (≈ 0.4 %/°C) | [S5] อ้าง Tani et al. ในญี่ปุ่น (ตัวเลขอ้างต่อ ไม่ได้อ่านต้นฉบับ) ผลของอุณหภูมิในห้องแอร์จึงเล็กมาก |
| `room_t`, `room_rh` ปัดเป็นจำนวนเต็ม | | DHT11 ละเอียด 1 °C / 1 % |

## ค่าที่เป็นสมมติฐาน (ไม่มีตัวเลขอ้างอิง ควรปรับเมื่อมีข้อมูลจริง)

| ค่า | ใช้ | เหตุผล |
|---|---|---|
| `WORK_SHARE = 0.5` | ครึ่งหนึ่งของน้ำทั้งวันดื่มที่โต๊ะ | ไม่พบงานวิจัยที่แยกตามเวลาทำงาน ([S1] บันทึกเวลาแต่ไม่ได้รายงาน) |
| กลืน 2–4.5 ครั้งต่อการยกแก้ว 1 ครั้ง | ได้ ~60 ml/ครั้ง, ~14 ครั้ง/วัน | [S2] ให้แค่ขนาดจิบ ไม่ได้ให้จำนวนจิบต่อครั้ง |
| ไม่ได้ดื่มนาน → โอกาสดื่มลดลง (`forget_strength`, `forget_tau_min`) | สมมติฐานหลักของโปรเจกต์ (ลืมดื่ม) | ยังไม่มีงานวิจัยยืนยัน |
| ช่วงยุ่ง ดื่มน้อยลงเหลือ 25–70 % (`busy_factor`) | [S1] มีแค่ข้อสังเกตว่าคนใส่ PPE นาน ๆ อาจดื่มน้อยลง | |
| กลับมาที่โต๊ะ โอกาสดื่ม ×2 ใน 10 นาที | | |
| ตารางเวลา: มา 8–9 โมง กลับ 17–18, พักเที่ยง 45–75 นาที (85 % ของวัน), ประชุม 0.3–2.5 ครั้ง/วัน, ลุกสั้น ๆ ทุก ~90 นาที | | |
| วันที่แอร์ไม่เย็น 10 % (+2–4 °C) | | ครอบคลุมกรณีนั่งทำงานที่หอ/บ้าน |
| ค่าเสียง KY-038: เงียบ ~130 ± 45, มีคนคุย ~380 ± 140 | | **ต้องวัดจากเซนเซอร์จริงแล้วแก้** เป็นค่า ADC peak-to-peak ขึ้นกับการหมุน trimmer |
| แก้ว 300–600 ml หนัก 180–380 g, noise load cell ± 1.5 g | ใช้สร้างคอลัมน์ `weight_g` | |

## คอลัมน์ใน office_hydration_v1.csv

| คอลัมน์ | ความหมาย |
|---|---|
| `person`, `day`, `time` | คนที่ 1–24, วันทำงานที่ 1–20, เวลา (ทุก 5 นาที) |
| `mins_since_drink` … `sound_p2p` | 7 feature ตรงกับ `ai_x` ที่ gateway บันทึก |
| `weight_g` | น้ำหนักที่ load cell น่าจะอ่านได้ (แก้ว + น้ำ) ใช้ทดสอบตัวตรวจจับการดื่มได้ |
| `busy` | ค่าจริงที่จำลอง (gateway มองไม่เห็นตรง ๆ) |
| `future_ml_60` | ดื่มจริงใน 60 นาทีถัดไป → label = `future_ml_60 < need_ml` |

## ข้อจำกัด
- ผล accuracy ที่ได้จาก dataset นี้บอกได้แค่ว่าโมเดลเรียนรู้รูปแบบที่สมมติไว้ ไม่ใช่ความแม่นกับคนจริง ให้เขียนแบบนี้ใน report
- ผู้ใช้จริงคนเดียว ข้อมูลจริงไม่กี่วันจะบอกได้ดีกว่า: ใช้ `python ai/train.py --firebase export.json --mix-synthetic`
  แล้วเทียบผลกับ dataset นี้

## แหล่งอ้างอิง
- [S1] Sunardi D. et al. (2022) Water and Beverages Intake Among Workers Amid the COVID-19 Pandemic in Indonesia. *Frontiers in Nutrition* 9:832641. https://www.frontiersin.org/journals/nutrition/articles/10.3389/fnut.2022.832641/pdf
- [S2] Bennett J.W. et al. (2009) Sip-Sizing Behaviors in Natural Drinking Conditions Compared to Instructed Experimental Conditions. *Dysphagia* 24:152–158. https://doi.org/10.1007/s00455-008-9183-y
- [S3] EUFIC, How much water should you drink per day? (สรุปค่า EFSA) https://www.eufic.org/en/archive/article/how-much-water-should-you-drink-per-day
- [S4] Aryal A., Chaiwiwatworakul P., Chirarattananon S., Wattanakit S. (2021) A Field Survey of Thermal Comfort in Air-conditioned Space in Songkhla's Hot Humid Climate. *Engineering Journal* 25(2). https://engj.eng.chula.ac.th/ej/article/download/4260/1085
- [S5] Zhang J. et al. (2025) Seasonal fluctuation of total water intake and hydration status among young men and women. *Frontiers in Nutrition* 11:1463501 (อ้าง Tani et al.) https://www.frontiersin.org/journals/nutrition/articles/10.3389/fnut.2024.1463501/full
- [S6] Gómez-Carmona O., Casado-Mansilla D. (2021) Office Hydration Monitoring (OHM) Dataset, Zenodo, CC BY 4.0. https://research.science.eus/documentos/668fc48bb9e7c03b01be092f?lang=en
- [S7] Cergolj V. et al. (2025) Drinking event detection on a sensing wristband using machine learning. *J. Ambient Intell. Smart Environ.* 17(2). https://journals.sagepub.com/doi/10.3233/AIS-230524
- ดูเพิ่ม: Morin C. et al. (2018) Liq.In7 drinking behavior in six countries. *Eur J Nutr* 57:101–112. https://link.springer.com/article/10.1007/s00394-018-1744-8 · Cohen R. et al. (2022) smart water bottle accuracy, *Sci Rep*. https://pmc.ncbi.nlm.nih.gov/articles/PMC8924188
