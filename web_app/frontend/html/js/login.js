document.addEventListener("DOMContentLoaded", () => {
  const loginForm = document.getElementById("login-form");
  const usernameInput = document.getElementById("username");
  const passwordInput = document.getElementById("password");
  const btnSubmit = document.getElementById("btn-submit");
  const btnText = btnSubmit.querySelector(".btn-text");
  const btnIcon = btnSubmit.querySelector(".btn-icon");
  const loginSpinner = document.getElementById("login-spinner");
  const btnTogglePwd = document.getElementById("btn-toggle-pwd");
  const eyeIcon = document.getElementById("eye-icon");

  // Kiểm tra nếu đã có token trong localStorage thì chuyển vào index.html
  const existingToken = localStorage.getItem("gate_token");
  if (existingToken) {
    // Có thể kiểm tra tính hợp lệ qua /api/auth/me
    fetch("/api/auth/me", {
      headers: { "Authorization": `Bearer ${existingToken}` }
    })
    .then(res => {
      if (res.ok) {
        window.location.href = "index.html";
      } else {
        localStorage.removeItem("gate_token");
      }
    })
    .catch(() => {});
  }

  // 1. Chức năng Hiện / Ẩn mật khẩu
  btnTogglePwd.addEventListener("click", () => {
    const isPassword = passwordInput.type === "password";
    passwordInput.type = isPassword ? "text" : "password";
    eyeIcon.innerHTML = isPassword
      ? `<path d="M17.94 17.94A10.07 10.07 0 0 1 12 20c-7 0-11-8-11-8a18.45 18.45 0 0 1 5.06-5.94M9.9 4.24A9.12 9.12 0 0 1 12 4c7 0 11 8 11 8a18.5 18.5 0 0 1-2.16 3.19m-6.72-1.07a3 3 0 1 1-4.24-4.24"></path><line x1="1" y1="1" x2="23" y2="23"></line>`
      : `<path d="M1 12s4-8 11-8 11 8 11 8-4 8-11 8-11-8-11-8z"></path><circle cx="12" cy="12" r="3"></circle>`;
  });

  // 2. Xử lý gửi Form Đăng Nhập
  loginForm.addEventListener("submit", async (e) => {
    e.preventDefault();

    const username = usernameInput.value.trim();
    const password = passwordInput.value;

    if (!username || !password) {
      showToast("Vui lòng điền đầy đủ tài khoản và mật khẩu!", "error");
      return;
    }

    // Hiển thị trạng thái đang xử lý (loading)
    btnSubmit.disabled = true;
    btnText.innerText = "Đang xác thực...";
    if (btnIcon) btnIcon.classList.add("hidden");
    if (loginSpinner) loginSpinner.classList.remove("hidden");

    try {
      const response = await fetch("/api/auth/login", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Accept": "application/json"
        },
        body: JSON.stringify({ username, password })
      });

      const data = await response.json();

      if (!response.ok) {
        throw new Error(data.detail || "Đăng nhập thất bại!");
      }

      // Lưu JWT Token và thông tin User
      localStorage.setItem("gate_token", data.access_token);
      localStorage.setItem("gate_user", JSON.stringify(data.user));

      showToast(`Chào mừng ${data.user.fullname || data.user.username}! Đăng nhập thành công.`, "success");

      // Chuyển hướng sang trang Dashboard
      setTimeout(() => {
        window.location.href = "index.html";
      }, 700);

    } catch (err) {
      showToast(err.message || "Không thể kết nối đến máy chủ!", "error");
      btnSubmit.disabled = false;
      btnText.innerText = "LOG IN";
      if (btnIcon) btnIcon.classList.remove("hidden");
      if (loginSpinner) loginSpinner.classList.add("hidden");
    }
  });

  // 3. Xử lý Quên mật khẩu
  const linkForgot = document.getElementById("link-forgot-pwd");
  if (linkForgot) {
    linkForgot.addEventListener("click", () => {
      showToast("Tài khoản mặc định: admin • Mật khẩu: admin123", "info");
    });
  }

  // 3. Hàm hiển thị thông báo Toast
  function showToast(message, type = "info") {
    const container = document.getElementById("toast-container");
    const toast = document.createElement("div");
    toast.className = `toast ${type}`;
    toast.innerHTML = `<span>${message}</span>`;

    container.appendChild(toast);

    setTimeout(() => {
      toast.style.opacity = "0";
      toast.style.transform = "translateX(50px)";
      toast.style.transition = "all 0.3s ease";
      setTimeout(() => toast.remove(), 300);
    }, 3500);
  }
});
