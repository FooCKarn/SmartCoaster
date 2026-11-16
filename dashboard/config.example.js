// คัดลอกไฟล์นี้เป็น config.js แล้ววาง firebaseConfig ของโปรเจกต์
// (Firebase console → Project settings → General → Your apps → Web app)
// apiKey ของเว็บไม่ใช่ความลับ แต่ความปลอดภัยต้องมาจาก database rules (ดู firebase/)
export const firebaseConfig = {
  apiKey: "YOUR_WEB_API_KEY",
  authDomain: "your-project-id.firebaseapp.com",
  databaseURL: "https://your-project-id-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId: "your-project-id",
  appId: "YOUR_APP_ID"
};
