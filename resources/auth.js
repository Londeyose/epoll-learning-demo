(function () {
    const toRegisterBtn = document.getElementById('to-register-btn');
    const toLoginBtn = document.getElementById('to-login-btn');
    const loginForm = document.getElementById('login-form');
    const registerForm = document.getElementById('register-form');

    if (toRegisterBtn) {
        toRegisterBtn.addEventListener('click', function () {
            window.location.href = '/register.html';
        });
    }

    if (toLoginBtn) {
        toLoginBtn.addEventListener('click', function () {
            window.location.href = '/login.html';
        });
    }

    if (loginForm) {
        loginForm.addEventListener('submit', function (e) {
            e.preventDefault();
            const formData = new FormData(loginForm);
            const username = (formData.get('username') || '').toString().trim();
            const password = (formData.get('password') || '').toString().trim();
            if (!username || !password) {
                alert('用户名或者密码不能为空！');
                return;
            }
            // 后端 /login 返回 HTML 页面，使用原生表单提交让浏览器直接跳转。
            loginForm.submit();
        });
    }

    if (registerForm) {
        registerForm.addEventListener('submit', function (e) {
            e.preventDefault();
            const formData = new FormData(registerForm);
            const username = (formData.get('username') || '').toString().trim();
            const password = (formData.get('password') || '').toString().trim();
            if (!username || !password) {
                alert('用户名或者密码不能为空！');
                return;
            }
            // 后端 /register 返回 HTML 页面，使用原生表单提交让浏览器直接跳转。
            registerForm.submit();
        });
    }
})();
