//! Pequenas utilidades de vetores (sem dependências externas, para compilar rápido).

#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Vec3 {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl Vec3 {
    pub const ZERO: Self = Self { x: 0.0, y: 0.0, z: 0.0 };

    pub const fn new(x: f32, y: f32, z: f32) -> Self {
        Self { x, y, z }
    }

    pub fn from_slice(v: &[f32]) -> Self {
        Self { x: v[0], y: v[1], z: v[2] }
    }

    pub fn to_array(self) -> [f32; 3] {
        [self.x, self.y, self.z]
    }

    pub fn horizontal(self) -> Self {
        Self::new(self.x, 0.0, self.z)
    }

    pub fn dot(self, other: Self) -> f32 {
        self.x * other.x + self.y * other.y + self.z * other.z
    }

    pub fn length_horizontal(self) -> f32 {
        (self.x * self.x + self.z * self.z).sqrt()
    }

    /// Normaliza o vetor horizontal; devolve zero se for muito pequeno.
    pub fn normalized_horizontal(self) -> Self {
        let len = self.length_horizontal();
        if len < 1e-5 {
            Self::ZERO
        } else {
            Self::new(self.x / len, 0.0, self.z / len)
        }
    }

    pub fn lerp(self, other: Self, t: f32) -> Self {
        Self::new(
            self.x + (other.x - self.x) * t,
            self.y + (other.y - self.y) * t,
            self.z + (other.z - self.z) * t,
        )
    }

    pub fn approx_eq(self, other: Self, eps: f32) -> bool {
        (self.x - other.x).abs() <= eps && (self.y - other.y).abs() <= eps && (self.z - other.z).abs() <= eps
    }
}

impl std::ops::Add for Vec3 {
    type Output = Self;
    fn add(self, rhs: Self) -> Self {
        Self::new(self.x + rhs.x, self.y + rhs.y, self.z + rhs.z)
    }
}

impl std::ops::AddAssign for Vec3 {
    fn add_assign(&mut self, rhs: Self) {
        *self = *self + rhs;
    }
}

impl std::ops::Sub for Vec3 {
    type Output = Self;
    fn sub(self, rhs: Self) -> Self {
        Self::new(self.x - rhs.x, self.y - rhs.y, self.z - rhs.z)
    }
}

impl std::ops::Mul<f32> for Vec3 {
    type Output = Self;
    fn mul(self, rhs: f32) -> Self {
        Self::new(self.x * rhs, self.y * rhs, self.z * rhs)
    }
}
